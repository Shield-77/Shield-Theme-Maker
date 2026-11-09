#include <3ds.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#define MAX_ENTRIES 256
#define PATH_SIZE 512
#define TEX_W 512
#define TEX_H 256
#define TEX_BYTES (TEX_W * TEX_H * 2)
#define BODY_HEADER 0x200
#define BODY_SIZE (BODY_HEADER + TEX_BYTES * 2)
#define BGM_LIMIT (3 * 1024 * 1024)

typedef struct { char name[256]; bool directory; } Entry;
static Entry entries[MAX_ENTRIES];
static int count, cursor, scroll, phase, fit_mode;
static int focus_x[2] = {50, 50}, focus_y[2] = {50, 50}, edit_target;
static stbi_uc *preview_pixels;
static int preview_width, preview_height, preview_target = -1;
static char cwd[PATH_SIZE] = "sdmc:/";
static char chosen[3][PATH_SIZE];
static char message[128] = "";

static int ends_with(const char *name, const char *ext) {
    size_t a = strlen(name), b = strlen(ext);
    if (a < b) return 0;
    return strcasecmp(name + a - b, ext) == 0;
}

static bool accepted(const char *name) {
    if (phase == 2) return ends_with(name, ".bcstm");
    return ends_with(name, ".png") || ends_with(name, ".jpg") || ends_with(name, ".jpeg");
}

static int compare_entries(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    if (x->directory != y->directory) return y->directory - x->directory;
    return strcasecmp(x->name, y->name);
}

static void scan_directory(void) {
    count = cursor = scroll = 0;
    DIR *dir = opendir(cwd);
    if (!dir) { snprintf(message, sizeof(message), "Dossier illisible"); return; }
    struct dirent *de;
    while ((de = readdir(dir)) && count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        char path[PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s%s%s", cwd, strlen(cwd) > 6 ? "/" : "", de->d_name) >= (int)sizeof(path)) continue;
        struct stat info;
        bool folder = false;
#ifdef DT_DIR
        folder = de->d_type == DT_DIR;
#endif
        if (!folder && stat(path, &info) == 0) folder = S_ISDIR(info.st_mode);
        // Some SD drivers cannot stat a root entry even when readdir found it.
        // Keep supported images visible; selection and preview validate them.
        if (!folder && !accepted(de->d_name)) continue;
        Entry *item = &entries[count++];
        snprintf(item->name, sizeof(item->name), "%s", de->d_name);
        item->directory = folder;
    }
    closedir(dir);
    qsort(entries, count, sizeof(Entry), compare_entries);
}

static void parent_directory(void) {
    if (strcmp(cwd, "sdmc:/") == 0) {
        if (phase > 0) { phase--; scan_directory(); }
        return;
    }
    char *slash = strrchr(cwd, '/');
    if (slash && slash > cwd + 5) *slash = 0;
    else strcpy(cwd, "sdmc:/");
    scan_directory();
}

static void enter_directory(const char *name) {
    char next[PATH_SIZE];
    if (snprintf(next, sizeof(next), "%s%s%s", cwd, strlen(cwd) > 6 ? "/" : "", name) >= (int)sizeof(next)) {
        strcpy(message, "Chemin trop long"); return;
    }
    strcpy(cwd, next);
    scan_directory();
}

static bool full_path(char *out, size_t size, const char *name) {
    return snprintf(out, size, "%s%s%s", cwd, strlen(cwd) > 6 ? "/" : "", name) < (int)size;
}

static bool image_info(const char *path, int *w, int *h) {
    int c;
    if (!stbi_info(path, w, h, &c)) return false;
    return *w > 0 && *h > 0 && *w <= 4096 && *h <= 4096 && (long long)*w * *h <= 2400000;
}

typedef struct { int first, second; unsigned blend; } AxisSample;
typedef struct { AxisSample x[TEX_W], y[TEX_H]; } SampleMap;

static int clamp_pixel(int pixel, int length) {
    return pixel < 0 ? 0 : pixel >= length ? length - 1 : pixel;
}

static void build_axis(AxisSample *axis, int length, int source_length,
                       int display_length, int64_t scale, bool contain, int focus) {
    int64_t viewport = (int64_t)display_length * scale;
    int64_t source = (int64_t)source_length << 16;
    int64_t origin = contain ? (source - viewport) / 2 :
                     (source > viewport ? (source - viewport) * focus / 100 : 0);
    for (int i = 0; i < length; ++i) {
        int64_t center = origin + ((int64_t)(2 * i + 1) * scale) / 2;
        if (contain && (center < 0 || center >= ((int64_t)source_length << 16))) {
            axis[i] = (AxisSample){-1, -1, 0};
        } else if (scale >= (2LL << 16)) {
            // Two samples per axis cover the source footprint when shrinking.
            axis[i] = (AxisSample){
                clamp_pixel((int)((center - scale / 4) >> 16), source_length),
                clamp_pixel((int)((center + scale / 4) >> 16), source_length), 128};
        } else {
            // Bilinear interpolation when the scale is small or the image grows.
            int64_t pixel_center = center - (1LL << 15);
            int first = (int)(pixel_center >> 16);
            unsigned blend = (unsigned)((pixel_center & 0xffff) >> 8);
            axis[i] = (AxisSample){clamp_pixel(first, source_length),
                                   clamp_pixel(first + 1, source_length), blend};
        }
    }
}

static void build_sample_map(SampleMap *map, int sw, int sh, int dw, int dh,
                             int width, int height, int image_index) {
    int64_t sx = ((int64_t)sw << 16) / dw;
    int64_t sy = ((int64_t)sh << 16) / dh;
    int64_t source_aspect = (int64_t)sw * dh;
    int64_t target_aspect = (int64_t)sh * dw;
    bool substantial_crop = source_aspect * 100 > target_aspect * 112 ||
                            target_aspect * 100 > source_aspect * 112;
    bool contain = fit_mode == 1 || (fit_mode == 0 && substantial_crop);
    int64_t scale = contain ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
    int fx = image_index < 0 ? 50 : focus_x[image_index];
    int fy = image_index < 0 ? 50 : focus_y[image_index];
    build_axis(map->x, width, sw, dw, scale, contain, fx);
    build_axis(map->y, height, sh, dh, scale, contain, fy);
}

static inline int composited_channel(const stbi_uc *pixel, int channel, int background) {
    return pixel[3] == 255 ? pixel[channel] :
        (pixel[channel] * pixel[3] + background * (255 - pixel[3])) / 255;
}

static inline void sample_image(const stbi_uc *src, int sw, const SampleMap *map, int x, int y, int *r, int *g, int *b) {
    const AxisSample *ax = &map->x[x], *ay = &map->y[y];
    if (ax->first < 0 || ay->first < 0) { *r = 6; *g = 11; *b = 39; return; }
    const stbi_uc *p00 = src + ((size_t)ay->first * sw + ax->first) * 4;
    const stbi_uc *p10 = src + ((size_t)ay->first * sw + ax->second) * 4;
    const stbi_uc *p01 = src + ((size_t)ay->second * sw + ax->first) * 4;
    const stbi_uc *p11 = src + ((size_t)ay->second * sw + ax->second) * 4;
    unsigned w00 = (256 - ax->blend) * (256 - ay->blend);
    unsigned w10 = ax->blend * (256 - ay->blend);
    unsigned w01 = (256 - ax->blend) * ay->blend;
    unsigned w11 = ax->blend * ay->blend;
    *r = (composited_channel(p00, 0, 6) * w00 + composited_channel(p10, 0, 6) * w10 +
          composited_channel(p01, 0, 6) * w01 + composited_channel(p11, 0, 6) * w11 + 32768) >> 16;
    *g = (composited_channel(p00, 1, 11) * w00 + composited_channel(p10, 1, 11) * w10 +
          composited_channel(p01, 1, 11) * w01 + composited_channel(p11, 1, 11) * w11 + 32768) >> 16;
    *b = (composited_channel(p00, 2, 39) * w00 + composited_channel(p10, 2, 39) * w10 +
          composited_channel(p01, 2, 39) * w01 + composited_channel(p11, 2, 39) * w11 + 32768) >> 16;
}

static bool render_top(const stbi_uc *src, int sw, int sh, int width, int image_index) {
    SampleMap map;
    int texture_width = width == 400 ? 412 : width;
    build_sample_map(&map, sw, sh, texture_width, 240, texture_width, 240, image_index);
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (!fb) return false;
    for (int x = 0; x < 400; ++x) for (int y = 0; y < 240; ++y) {
        int r, g, b;
        int display_x = x - (400 - width) / 2;
        if (display_x >= 0 && display_x < width)
            sample_image(src, sw, &map, display_x + (texture_width - width) / 2, y, &r, &g, &b);
        else { r = 6; g = 11; b = 39; }
        size_t p = ((size_t)x * 240 + (239 - y)) * 3;
        fb[p] = b; fb[p+1] = g; fb[p+2] = r;
    }
    gfxFlushBuffers();
    return true;
}

static bool show_top(const char *path, int width, int image_index) {
    int sw, sh, c;
    stbi_uc *src = stbi_load(path, &sw, &sh, &c, 4);
    if (!src) return false;
    bool ok = render_top(src, sw, sh, width, image_index);
    stbi_image_free(src);
    return ok;
}

static void clear_preview(void) {
    stbi_image_free(preview_pixels);
    preview_pixels = NULL;
    preview_target = -1;
}

static bool preview_selected(void) {
    if (preview_target != edit_target || !preview_pixels) {
        clear_preview();
        int channels;
        preview_pixels = stbi_load(chosen[edit_target], &preview_width, &preview_height, &channels, 4);
        if (!preview_pixels) return false;
        preview_target = edit_target;
    }
    return render_top(preview_pixels, preview_width, preview_height,
                      edit_target == 0 ? 400 : 320, edit_target);
}

static void show_splash(void) {
    int previous = fit_mode;
    fit_mode = 2;
    show_top("romfs:/splash.png", 400, -1);
    fit_mode = previous;
}

static void put_u32(u8 *p, u32 v) {
    p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = v >> 24;
}

static bool fill_texture(u8 *dest, const char *path, int display_w, int image_index) {
    int sw, sh, c;
    stbi_uc *src = stbi_load(path, &sw, &sh, &c, 4);
    if (!src) return false;
    SampleMap map;
    build_sample_map(&map, sw, sh, display_w, 240, display_w, 240, image_index);
    u16 *pixels = (u16 *)dest;
    for (int y = 0; y < TEX_H; ++y) for (int x = 0; x < TEX_W; ++x) {
        int r, g, b;
        int px = x < display_w ? x : display_w - 1;
        int py = y < 240 ? y : 239;
        sample_image(src, sw, &map, px, py, &r, &g, &b);
        unsigned tile = (((y >> 3) * (TEX_W >> 3) + (x >> 3)) << 6)
            + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
        pixels[tile] = (u16)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
    stbi_image_free(src);
    return true;
}

static bool write_lz11_literals(FILE *out, const u8 *data, size_t size) {
    u8 header[4] = { 0x11, size & 255, (size >> 8) & 255, (size >> 16) & 255 };
    if (fwrite(header, 1, 4, out) != 4) return false;
    for (size_t p = 0; p < size; p += 8) {
        u8 flag = 0;
        size_t length = size - p < 8 ? size - p : 8;
        if (fwrite(&flag, 1, 1, out) != 1 || fwrite(data + p, 1, length, out) != length) return false;
    }
    return fflush(out) == 0;
}

static bool valid_bgm(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) return false;
    u8 magic[4];
    bool ok = fread(magic, 1, 4, in) == 4 && memcmp(magic, "CSTM", 4) == 0;
    if (fseek(in, 0, SEEK_END) != 0) ok = false;
    long size = ftell(in);
    fclose(in);
    return ok && size > 0 && size <= BGM_LIMIT;
}

static bool find_input_image(char *out, const char *base) {
    const char *extensions[] = { ".png", ".jpg", ".jpeg" };
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        snprintf(out, PATH_SIZE, "sdmc:/3ds/ShieldThemeMaker/input/%s%s", base, extensions[i]);
        int w, h;
        if (image_info(out, &w, &h)) return true;
    }
    out[0] = 0;
    return false;
}

static bool automatic_pair(void) {
    if (!find_input_image(chosen[0], "haut") && !find_input_image(chosen[0], "top")) {
        strcpy(message, "Auto: haut.png ou top.png manquant"); return false;
    }
    if (!find_input_image(chosen[1], "bas") && !find_input_image(chosen[1], "bottom")) {
        strcpy(message, "Auto: bas.png ou bottom.png manquant"); return false;
    }
    snprintf(chosen[2], PATH_SIZE, "sdmc:/3ds/ShieldThemeMaker/input/bgm.bcstm");
    if (!valid_bgm(chosen[2])) chosen[2][0] = 0;
    phase = 3;
    message[0] = 0;
    return true;
}

static bool copy_bgm(const char *source, const char *dest) {
    FILE *in = fopen(source, "rb"), *out = fopen(dest, "wb");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return false; }
    u8 buffer[8192]; size_t n; bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0)
        if (fwrite(buffer, 1, n, out) != n) { ok = false; break; }
    if (ferror(in) || fflush(out) != 0) ok = false;
    fclose(in); fclose(out);
    return ok;
}

static void theme_name(char *out, size_t size) {
    const char *base = strrchr(chosen[0], '/'); base = base ? base + 1 : chosen[0];
    snprintf(out, size, "Shield_");
    size_t used = strlen(out);
    for (; *base && *base != '.' && used < size - 1 && used < 28; ++base) {
        char c = *base;
        out[used++] = isalnum((unsigned char)c) ? c : '_';
    }
    out[used] = 0;
}

static bool generate_theme(void) {
    int w, h;
    if (!image_info(chosen[0], &w, &h) || !image_info(chosen[1], &w, &h)) {
        strcpy(message, "Image invalide ou trop grande"); return false;
    }
    if (chosen[2][0] && !valid_bgm(chosen[2])) {
        strcpy(message, "BCSTM invalide ou > 3 Mo"); return false;
    }
    // Anemone3DS scans the SD card's /themes/ directory.
    mkdir("sdmc:/themes", 0777);
    char name[48], folder[128], body_path[160], bgm_path[160];
    theme_name(name, sizeof(name));
    int suffix = 0;
    do {
        if (suffix) snprintf(folder, sizeof(folder), "sdmc:/themes/%s_%d", name, suffix);
        else snprintf(folder, sizeof(folder), "sdmc:/themes/%s", name);
        ++suffix;
    } while (suffix < 100 && access(folder, F_OK) == 0);
    if (suffix == 100 || mkdir(folder, 0777) != 0) { strcpy(message, "Creation dossier impossible"); return false; }
    snprintf(body_path, sizeof(body_path), "%s/body_LZ.bin", folder);
    snprintf(bgm_path, sizeof(bgm_path), "%s/bgm.bcstm", folder);
    u8 *body = calloc(1, BODY_SIZE);
    if (!body) { rmdir(folder); strcpy(message, "Memoire insuffisante"); return false; }
    put_u32(body, 1);
    body[5] = chosen[2][0] ? 1 : 0;
    put_u32(body + 0x0c, 3); put_u32(body + 0x10, 1); put_u32(body + 0x18, BODY_HEADER);
    put_u32(body + 0x20, 3); put_u32(body + 0x24, 1); put_u32(body + 0x28, BODY_HEADER + TEX_BYTES);
    bool ok = fill_texture(body + BODY_HEADER, chosen[0], 412, 0)
           && fill_texture(body + BODY_HEADER + TEX_BYTES, chosen[1], 320, 1);
    if (ok) {
        FILE *out = fopen(body_path, "wb");
        ok = out && write_lz11_literals(out, body, BODY_SIZE);
        if (out && fclose(out) != 0) ok = false;
    }
    free(body);
    if (ok && chosen[2][0]) ok = copy_bgm(chosen[2], bgm_path);
    if (!ok) {
        remove(body_path); remove(bgm_path); rmdir(folder);
        strcpy(message, "Export echoue (SD / memoire)"); return false;
    }
    snprintf(message, sizeof(message), "Cree: themes/%s", strrchr(folder, '/') + 1);
    return true;
}

static const char *short_name(const char *path) {
    const char *p = strrchr(path, '/'); return p ? p + 1 : path;
}

static void draw_ui(void) {
    consoleClear();
    printf("\x1b[0;0H SHIELD THEME MAKER  v0.1.2\n");
    printf(" 100%% sur la console / old 3DS\n");
    printf("----------------------------------------\n");
    if (phase < 3) {
        printf(" Etape %d/3: %s\n", phase + 1, phase == 0 ? "image du haut" : phase == 1 ? "image du bas" : "musique BCSTM");
        printf(" Haut: %.31s\n", chosen[0][0] ? short_name(chosen[0]) : "(aucune)");
        printf(" Bas : %.31s\n", chosen[1][0] ? short_name(chosen[1]) : "(aucune)");
        printf(" BGM : %.31s\n", chosen[2][0] ? short_name(chosen[2]) : "(sans musique)");
        printf("----------------------------------------\n");
        printf(" %.39s\n", cwd);
        for (int i = scroll; i < count && i < scroll + 15; ++i)
            printf("%c %c %.35s\n", i == cursor ? '>' : ' ', entries[i].directory ? '+' : ' ', entries[i].name);
        if (!count) printf(" (aucun fichier compatible ici)\n");
        printf("\x1b[24;0H%.39s", message);
        printf("\x1b[26;0HA choisir  Y apercu  B retour\n");
        if (phase == 0) printf("X detection auto: haut/bas + bgm\n");
        if (phase == 2) printf("X passer la musique\n");
    } else {
        printf(" %s\n", phase == 3 ? "Pret a creer le theme" : "Theme cree !");
        printf("\n Haut: %.33s\n", short_name(chosen[0]));
        printf(" Bas : %.33s\n", short_name(chosen[1]));
        printf(" BGM : %.33s\n", chosen[2][0] ? short_name(chosen[2]) : "sans musique");
        printf(" Mode: %s\n", fit_mode == 0 ? "auto intelligent" : fit_mode == 1 ? "image entiere" : "recadrage centre");
        if (phase == 3) {
            printf(" Reglage: %s\n", edit_target == 0 ? "ecran haut" : "ecran bas");
            printf(" Cadrage: H %d%%  V %d%%\n", focus_x[edit_target], focus_y[edit_target]);
        }
        printf("\n %s\n", message);
        if (phase == 3) {
            printf("\n L/R ecran  Croix deplacer\n");
            printf(" Y apercu  X mode  A creer  B retour\n");
        }
        else printf("\n A nouveau theme   START quitter\n");
    }
}

int main(void) {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_BOTTOM, NULL);
    if (romfsInit() == 0) show_splash();
    DIR *sd = opendir("sdmc:/");
    if (!sd) {
        consoleClear(); printf("Carte SD indisponible.\nSTART pour quitter.");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); }
        romfsExit(); gfxExit(); return 1;
    }
    closedir(sd);
    mkdir("sdmc:/3ds/ShieldThemeMaker", 0777);
    mkdir("sdmc:/3ds/ShieldThemeMaker/input", 0777);
    scan_directory(); draw_ui();
    while (aptMainLoop()) {
        hidScanInput(); u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        bool redraw = false;
        if (phase < 3) {
            if ((keys & KEY_UP) && cursor > 0) { cursor--; if (cursor < scroll) scroll = cursor; redraw = true; }
            if ((keys & KEY_DOWN) && cursor + 1 < count) { cursor++; if (cursor >= scroll + 15) scroll = cursor - 14; redraw = true; }
            if (keys & KEY_B) { parent_directory(); redraw = true; }
            if ((keys & KEY_X) && phase == 0) { automatic_pair(); redraw = true; }
            if ((keys & KEY_X) && phase == 2) { chosen[2][0] = 0; phase = 3; redraw = true; }
            if ((keys & KEY_Y) && phase < 2 && count && !entries[cursor].directory) {
                char path[PATH_SIZE]; int width, height;
                if (full_path(path, sizeof(path), entries[cursor].name) &&
                    image_info(path, &width, &height) && show_top(path, phase == 0 ? 400 : 320, phase))
                    strcpy(message, "Apercu: A pour choisir");
                else strcpy(message, "Apercu impossible");
                redraw = true;
            }
            if ((keys & KEY_A) && phase < 3 && count) {
                if (entries[cursor].directory) enter_directory(entries[cursor].name);
                else {
                    char path[PATH_SIZE];
                    int width, height;
                    if (!full_path(path, sizeof(path), entries[cursor].name)) {
                        strcpy(message, "Chemin trop long");
                    } else if (phase == 2 || image_info(path, &width, &height)) {
                        strcpy(chosen[phase], path);
                        phase++;
                        scan_directory();
                        message[0] = 0;
                    } else strcpy(message, "Image invalide ou trop grande");
                }
                redraw = true;
            }
        } else if (phase == 3) {
            if (keys & KEY_B) {
                clear_preview(); phase = 2; scan_directory(); redraw = true;
            } else {
                if (keys & KEY_L) { edit_target = 0; redraw = true; }
                if (keys & KEY_R) { edit_target = 1; redraw = true; }
                if (keys & KEY_X) { fit_mode = (fit_mode + 1) % 3; redraw = true; }
                int old_x = focus_x[edit_target], old_y = focus_y[edit_target];
                if (keys & KEY_LEFT) focus_x[edit_target] -= 10;
                if (keys & KEY_RIGHT) focus_x[edit_target] += 10;
                if (keys & KEY_UP) focus_y[edit_target] -= 10;
                if (keys & KEY_DOWN) focus_y[edit_target] += 10;
                if (focus_x[edit_target] < 0) focus_x[edit_target] = 0;
                if (focus_x[edit_target] > 100) focus_x[edit_target] = 100;
                if (focus_y[edit_target] < 0) focus_y[edit_target] = 0;
                if (focus_y[edit_target] > 100) focus_y[edit_target] = 100;
                bool moved = old_x != focus_x[edit_target] || old_y != focus_y[edit_target];
                if (moved) { fit_mode = 2; redraw = true; }
                if ((keys & (KEY_Y | KEY_L | KEY_R | KEY_X)) || moved) {
                    if (!preview_selected()) strcpy(message, "Apercu impossible");
                    else message[0] = 0;
                    redraw = true;
                }
                if (keys & KEY_A) {
                    clear_preview(); strcpy(message, "Generation en cours..."); draw_ui();
                    if (generate_theme()) phase = 4;
                    redraw = true;
                }
            }
        } else if (keys & KEY_A) {
            clear_preview(); memset(chosen, 0, sizeof(chosen)); phase = 0; fit_mode = 0;
            edit_target = 0; focus_x[0] = focus_x[1] = focus_y[0] = focus_y[1] = 50;
            strcpy(cwd, "sdmc:/");
            scan_directory(); show_splash(); redraw = true;
        }
        if (redraw) draw_ui();
        gfxFlushBuffers(); gspWaitForVBlank();
    }
    clear_preview(); romfsExit(); gfxExit();
    return 0;
}
