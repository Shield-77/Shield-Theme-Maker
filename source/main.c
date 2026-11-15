#include <3ds.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>
#include <3ds/util/utf.h>
#include <3ds/archive.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include "stbi_image_write.h"
#include "audio_convert.h"
#include "bcstm_validate.h"
#include "zip_store.h"
#include "ui.h"
#include "locale.h"

#define MAX_ENTRIES 512
#define PATH_SIZE 512
#define TEX_W 512
#define TEX_H 256
#define TEX_BYTES (TEX_W * TEX_H * 2)
#define BODY_HEADER 0x200
#define BODY_SIZE (BODY_HEADER + TEX_BYTES * 2)
#define BGM_LIMIT 0x337000u
#define LANGUAGE_PATH "sdmc:/3ds/ShieldThemeMaker/ui-language.dat"
#define GUIDE_PATH "sdmc:/3ds/ShieldThemeMaker/guide-seen.dat"
#define GUIDE_VERSION '2'
#define GUIDE_PAGES 8

typedef struct { char name[256]; u64 modified; bool directory, dated; } Entry;
static Entry entries[MAX_ENTRIES];
static int count, cursor, scroll, phase, fit_mode[2];
static int zoom_percent[2] = {100, 100};
static int focus_x[2] = {50, 50}, focus_y[2] = {50, 50}, edit_target;
static stbi_uc *preview_pixels;
static int preview_width, preview_height, preview_target = -1;
static char preview_path[PATH_SIZE];
static bool focus_ready[2], new3ds;
static char cwd[PATH_SIZE] = "sdmc:/";
static char chosen[3][PATH_SIZE];
static int chosen_w[2], chosen_h[2];
static char message[128] = "";
static AudioInfo audio_info;
static uint32_t loop_start_ms, loop_end_ms, fade_ms = 500;
static unsigned audio_quality = 4;
static uint32_t audio_max_ms(void) { return audio_profile(audio_quality).max_ms; }
static bool custom_loop, export_zip, icon_from_bottom, audio_failed_prompt;
static bool candidate_ready;
static char candidate_path[PATH_SIZE];
static char custom_theme_name[48];
static bool scan_truncated;
static FS_DirectoryEntry scan_batch[32];
static int date_index, date_done, date_total;
static u64 date_started;
static bool render_top(const stbi_uc *src, int sw, int sh, int width, int image_index);
static void show_splash(void);

/* The lower screen is composed offscreen, then shown as one complete frame.
   Keep both buffers identical so status overlays preserve the existing menu. */
static void present_frame(void) {
    u8 *completed = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    gspWaitForVBlank();
    u8 *next = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (completed && next && completed != next)
        memcpy(next, completed, 320u * 240u * 3u);
}

static int ends_with(const char *name, const char *ext) {
    size_t a = strlen(name), b = strlen(ext);
    if (a < b) return 0;
    return strcasecmp(name + a - b, ext) == 0;
}

static bool accepted(const char *name) {
    if (phase == 2) return ends_with(name, ".bcstm") ||
                           ends_with(name, ".ogg") || ends_with(name, ".mp3");
    return ends_with(name, ".png") || ends_with(name, ".jpg") || ends_with(name, ".jpeg");
}

static int compare_entries(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    if (x->directory != y->directory) return y->directory - x->directory;
    if (!x->directory && x->dated != y->dated) return y->dated - x->dated;
    if (!x->directory && x->modified != y->modified)
        return x->modified < y->modified ? 1 : -1;
    return strcasecmp(x->name, y->name);
}

static bool scan_directory_fast(void) {
    FS_Archive archive;
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""))))
        return false;
    u16 path[PATH_SIZE] = {0};
    ssize_t length = utf8_to_utf16(path, (const u8 *)(cwd + 5), PATH_SIZE - 1);
    if (length < 0 || length >= PATH_SIZE) { FSUSER_CloseArchive(archive); return false; }
    path[length] = 0;
    Handle directory;
    Result result = FSUSER_OpenDirectory(&directory, archive, fsMakePath(PATH_UTF16, path));
    if (R_FAILED(result)) { FSUSER_CloseArchive(archive); return false; }
    bool okay = true;
    for (;;) {
        u32 read = 0;
        result = FSDIR_Read(directory, &read, 32, scan_batch);
        if (R_FAILED(result)) { okay = false; break; }
        if (!read) break;
        for (u32 i = 0; i < read; ++i) {
            bool folder = (scan_batch[i].attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
            char name[256];
            ssize_t bytes = utf16_to_utf8((u8 *)name, scan_batch[i].name, sizeof(name) - 1);
            if (bytes < 0 || bytes >= (ssize_t)sizeof(name)) continue;
            name[bytes] = 0;
            if (name[0] == '.' || (!folder && !accepted(name))) continue;
            if (count >= MAX_ENTRIES) { scan_truncated = true; continue; }
            Entry *item = &entries[count++];
            strcpy(item->name, name);
            item->directory = folder;
            item->modified = 0;
            item->dated = false;
        }
    }
    FSDIR_Close(directory);
    FSUSER_CloseArchive(archive);
    return okay;
}

static void scan_directory(void) {
    count = cursor = scroll = 0;
    date_index = date_done = date_total = 0;
    date_started = osGetTime();
    scan_truncated = false;
    if (scan_directory_fast()) {
        qsort(entries, count, sizeof(Entry), compare_entries);
        for (int i = 0; i < count; ++i) if (!entries[i].directory) ++date_total;
        return;
    }
    count = 0;
    scan_truncated = false;
    DIR *dir = opendir(cwd);
    if (!dir) { snprintf(message, sizeof(message), "Dossier illisible"); return; }
    struct dirent *de;
    while ((de = readdir(dir))) {
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
        if (count >= MAX_ENTRIES) { scan_truncated = true; continue; }
        Entry *item = &entries[count++];
        snprintf(item->name, sizeof(item->name), "%s", de->d_name);
        item->directory = folder;
        item->modified = 0;
        item->dated = false;
    }
    closedir(dir);
    qsort(entries, count, sizeof(Entry), compare_entries);
    for (int i = 0; i < count; ++i) if (!entries[i].directory) ++date_total;
}

static bool update_file_dates(void) {
    if (date_done >= date_total || phase >= 3) return false;
    for (int budget = 0; budget < 2 && date_index < count; ++date_index) {
        Entry *item = &entries[date_index];
        if (item->directory) continue;
        char path[PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s%s%s", cwd,
                     strlen(cwd) > 6 ? "/" : "", item->name) < (int)sizeof(path)) {
            u64 stamp = 0;
            if (R_SUCCEEDED(archive_getmtime(path, &stamp))) {
                item->modified = stamp;
                item->dated = true;
            }
        }
        ++date_done;
        ++budget;
    }
    if (date_done < date_total) {
        ui_top_status("Tri par date", date_done*100/date_total,
                      (unsigned)((osGetTime()-date_started)/1000), (unsigned)(osGetTime()/250));
        present_frame();
        return date_done % 10 == 0;
    }
    char selected[256];
    bool selected_dir = count && entries[cursor].directory;
    snprintf(selected, sizeof(selected), "%s", count ? entries[cursor].name : "");
    qsort(entries, count, sizeof(Entry), compare_entries);
    for (int i = 0; i < count; ++i)
        if (entries[i].directory == selected_dir && strcmp(entries[i].name, selected) == 0) {
            cursor = i; break;
        }
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + 5) scroll = cursor - 4;
    if (preview_pixels && preview_target >= 0)
        render_top(preview_pixels, preview_width, preview_height,
                   preview_target == 0 ? 400 : 320, preview_target);
    else show_splash();
    return true;
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

typedef struct {
    const char *path;
    int *width, *height, *channels;
    stbi_uc *pixels;
    volatile bool done;
} ImageJob;

static void image_worker(void *arg) {
    ImageJob *job = arg;
    job->pixels = stbi_load(job->path, job->width, job->height, job->channels, 4);
    job->done = true;
}

static stbi_uc *load_image(const char *path, int *w, int *h, int *channels) {
    if (strncmp(path, "romfs:/", 7) == 0) return stbi_load(path, w, h, channels, 4);
    ImageJob job = {.path = path, .width = w, .height = h, .channels = channels};
    Thread worker = threadCreate(image_worker, &job, 0x20000, 0x30, -2, false);
    if (!worker) return stbi_load(path, w, h, channels, 4);
    u64 start = osGetTime();
    unsigned tick = 0;
    while (!job.done) {
        ui_top_status("Chargement image", -1, (unsigned)((osGetTime()-start)/1000), tick++);
        present_frame();
        gspWaitForVBlank();
    }
    threadJoin(worker, U64_MAX);
    threadFree(worker);
    return job.pixels;
}

static int whole_image_zoom(int image_index) {
    int sw = chosen_w[image_index], sh = chosen_h[image_index];
    if (!sw || !sh) return 100;
    int dw = image_index == 0 ? 412 : 320;
    int64_t sx = ((int64_t)sw << 16) / dw;
    int64_t sy = ((int64_t)sh << 16) / 240;
    int64_t cover = sx < sy ? sx : sy;
    int64_t contain = sx > sy ? sx : sy;
    int zoom = (int)(cover * 100 / contain);
    return zoom < 1 ? 1 : zoom;
}

static int clamp_focus(int value) { return value < 0 ? 0 : value > 100 ? 100 : value; }

/* Lightweight detail centroid: no model, network, or extra image decode. */
static void auto_focus_image(const stbi_uc *src, int sw, int sh, int image_index) {
    enum { GRID_X = 24, GRID_Y = 18 };
    int lum[GRID_Y][GRID_X], sat[GRID_Y][GRID_X];
    for (int y = 0; y < GRID_Y; ++y) for (int x = 0; x < GRID_X; ++x) {
        int px = (2*x+1)*sw/(2*GRID_X), py = (2*y+1)*sh/(2*GRID_Y);
        const stbi_uc *p = src + ((size_t)py*sw + px)*4;
        int hi = p[0] > p[1] ? p[0] : p[1];
        int lo = p[0] < p[1] ? p[0] : p[1];
        if (p[2] > hi) hi = p[2];
        if (p[2] < lo) lo = p[2];
        lum[y][x] = (p[0]*3 + p[1]*6 + p[2])/10;
        sat[y][x] = hi - lo;
    }
    u64 weight_sum = 0, x_sum = 0, y_sum = 0;
    for (int y = 1; y < GRID_Y-1; ++y) for (int x = 1; x < GRID_X-1; ++x) {
        int dx = abs(lum[y][x+1] - lum[y][x-1]);
        int dy = abs(lum[y+1][x] - lum[y-1][x]);
        int center = 80 - abs(2*x+1-GRID_X) - abs(2*y+1-GRID_Y);
        u32 weight = (32 + dx + dy + sat[y][x]/3) * center;
        weight_sum += weight;
        x_sum += (u64)(2*x+1) * weight;
        y_sum += (u64)(2*y+1) * weight;
    }
    int target_x = weight_sum ? (int)(x_sum * sw / (2*GRID_X*weight_sum)) : sw/2;
    int target_y = weight_sum ? (int)(y_sum * sh / (2*GRID_Y*weight_sum)) : sh/2;
    int dw = image_index == 0 ? 412 : 320;
    int64_t sx = ((int64_t)sw << 16)/dw, sy = ((int64_t)sh << 16)/240;
    int64_t scale = (sx < sy ? sx : sy) * 100 / zoom_percent[image_index];
    int viewport_x = (int)((dw * scale) >> 16);
    int viewport_y = (int)((240 * scale) >> 16);
    if (fit_mode[image_index] == 2) {
        int margin_x = viewport_x/4, margin_y = viewport_y/4;
        int min_x = (sw > viewport_x ? 0 : sw-viewport_x) - margin_x;
        int max_x = (sw > viewport_x ? sw-viewport_x : 0) + margin_x;
        int min_y = (sh > viewport_y ? 0 : sh-viewport_y) - margin_y;
        int max_y = (sh > viewport_y ? sh-viewport_y : 0) + margin_y;
        focus_x[image_index] = clamp_focus((target_x-viewport_x/2-min_x)*100/(max_x-min_x));
        focus_y[image_index] = clamp_focus((target_y-viewport_y/2-min_y)*100/(max_y-min_y));
    } else {
        focus_x[image_index] = viewport_x < sw ?
            clamp_focus((target_x - viewport_x/2)*100/(sw-viewport_x)) : 50;
        focus_y[image_index] = viewport_y < sh ?
            clamp_focus((target_y - viewport_y/2)*100/(sh-viewport_y)) : 50;
    }
    focus_ready[image_index] = true;
}

typedef struct { int index[4]; unsigned weight[4], count; } AxisSample;
typedef struct { AxisSample x[TEX_W], y[TEX_H]; } SampleMap;
/* A SampleMap is about 27 KiB: keep it off the small old-3DS main stack. */
static SampleMap sample_map;

static int clamp_pixel(int pixel, int length) {
    return pixel < 0 ? 0 : pixel >= length ? length - 1 : pixel;
}

static void build_axis(AxisSample *axis, int length, int source_length,
                       int display_length, int64_t scale, bool contain, bool extend, int focus,
                       bool quick) {
    int64_t viewport = (int64_t)display_length * scale;
    int64_t source = (int64_t)source_length << 16;
    bool outside = contain || (viewport > source && !extend);
    int64_t origin;
    if (extend) {
        int64_t margin = viewport / 4;
        int64_t minimum = (source > viewport ? 0 : source - viewport) - margin;
        int64_t maximum = (source > viewport ? source - viewport : 0) + margin;
        origin = minimum + (maximum - minimum) * focus / 100;
    } else {
        origin = viewport > source ? (source - viewport) / 2 :
                 (source > viewport ? (source - viewport) * focus / 100 : 0);
    }
    for (int i = 0; i < length; ++i) {
        int64_t center = origin + ((int64_t)(2 * i + 1) * scale) / 2;
        if (outside && (center < 0 || center >= source)) {
            axis[i] = (AxisSample){.index = {-1}, .count = 0};
        } else if (scale >= (2LL << 16)) {
            // The screen preview uses four samples per pixel; exports use sixteen.
            axis[i].count = quick ? 2 : 4;
            for (unsigned j = 0; j < axis[i].count; ++j) {
                int offset = quick ? (2 * (int)j - 1) : (2 * (int)j - 3);
                axis[i].index[j] = clamp_pixel((int)((center + offset * scale / (quick ? 4 : 8)) >> 16), source_length);
                axis[i].weight[j] = quick ? 128 : 64;
            }
        } else {
            // Bilinear interpolation when the scale is small or the image grows.
            int64_t pixel_center = center - (1LL << 15);
            int first = (int)(pixel_center >> 16);
            unsigned blend = (unsigned)((pixel_center & 0xffff) >> 8);
            axis[i] = (AxisSample){.index = {clamp_pixel(first, source_length),
                                             clamp_pixel(first + 1, source_length)},
                                    .weight = {256 - blend, blend}, .count = 2};
        }
    }
}

static void build_sample_map(SampleMap *map, int sw, int sh, int dw, int dh,
                             int width, int height, int image_index, bool quick) {
    int64_t sx = ((int64_t)sw << 16) / dw;
    int64_t sy = ((int64_t)sh << 16) / dh;
    bool contain = image_index >= 0 && fit_mode[image_index] == 1;
    bool extend = image_index >= 0 && fit_mode[image_index] == 2;
    int64_t cover_scale = sx < sy ? sx : sy;
    int64_t scale = contain ? (sx > sy ? sx : sy) :
                    cover_scale * 100 / (image_index < 0 ? 100 : zoom_percent[image_index]);
    int fx = image_index < 0 ? 50 : focus_x[image_index];
    int fy = image_index < 0 ? 50 : focus_y[image_index];
    build_axis(map->x, width, sw, dw, scale, contain, extend, fx, quick);
    build_axis(map->y, height, sh, dh, scale, contain, extend, fy, quick);
}

static inline int composited_channel(const stbi_uc *pixel, int channel, int background) {
    return pixel[3] == 255 ? pixel[channel] :
        (pixel[channel] * pixel[3] + background * (255 - pixel[3])) / 255;
}

static inline void sample_image(const stbi_uc *src, int sw, const SampleMap *map, int x, int y, int *r, int *g, int *b) {
    const AxisSample *ax = &map->x[x], *ay = &map->y[y];
    if (!ax->count || !ay->count) { *r = 6; *g = 11; *b = 39; return; }
    unsigned sum[3] = {0, 0, 0};
    for (unsigned j = 0; j < ay->count; ++j)
        for (unsigned i = 0; i < ax->count; ++i) {
            const stbi_uc *pixel = src + ((size_t)ay->index[j] * sw + ax->index[i]) * 4;
            unsigned weight = ay->weight[j] * ax->weight[i];
            sum[0] += composited_channel(pixel, 0, 6) * weight;
            sum[1] += composited_channel(pixel, 1, 11) * weight;
            sum[2] += composited_channel(pixel, 2, 39) * weight;
        }
    *r = (sum[0] + 32768) >> 16;
    *g = (sum[1] + 32768) >> 16;
    *b = (sum[2] + 32768) >> 16;
}

static bool render_top(const stbi_uc *src, int sw, int sh, int width, int image_index) {
    int texture_width = width == 400 ? 412 : width;
    build_sample_map(&sample_map, sw, sh, texture_width, 240, texture_width, 240,
                     image_index, !new3ds);
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (!fb) return false;
    for (int x = 0; x < 400; ++x) for (int y = 0; y < 240; ++y) {
        int r, g, b;
        int display_x = x - (400 - width) / 2;
        if (display_x >= 0 && display_x < width)
            sample_image(src, sw, &sample_map, display_x + (texture_width - width) / 2, y, &r, &g, &b);
        else { r = 6; g = 11; b = 39; }
        size_t p = ((size_t)x * 240 + (239 - y)) * 3;
        if (image_index >= 0 && (x < 3 || x >= 397 || y < 3 || y >= 237)) {
            r = image_index == 0 ? 55 : 255;
            g = image_index == 0 ? 174 : 194;
            b = image_index == 0 ? 255 : 44;
        }
        fb[p] = b; fb[p+1] = g; fb[p+2] = r;
    }
    present_frame();
    return true;
}

static bool cache_top(const char *path, int image_index) {
    if (preview_target == image_index && preview_pixels && strcmp(preview_path, path) == 0)
        return true;
    stbi_image_free(preview_pixels);
    preview_pixels = NULL;
    preview_target = -1;
    int channels;
    preview_pixels = load_image(path, &preview_width, &preview_height, &channels);
    if (!preview_pixels) return false;
    snprintf(preview_path, sizeof(preview_path), "%s", path);
    preview_target = image_index;
    return true;
}

static bool show_top(const char *path, int width, int image_index) {
    if (image_index >= 0) {
        if (!cache_top(path, image_index)) return false;
        return render_top(preview_pixels, preview_width, preview_height, width, image_index);
    }
    int sw, sh, c;
    stbi_uc *src = load_image(path, &sw, &sh, &c);
    if (!src) return false;
    bool ok = render_top(src, sw, sh, width, image_index);
    stbi_image_free(src);
    return ok;
}

static void clear_preview(void) {
    stbi_image_free(preview_pixels);
    preview_pixels = NULL;
    preview_target = -1;
    preview_path[0] = 0;
}

static bool preview_selected(void) {
    if (!cache_top(chosen[edit_target], edit_target)) return false;
    if (!focus_ready[edit_target])
        auto_focus_image(preview_pixels, preview_width, preview_height, edit_target);
    return render_top(preview_pixels, preview_width, preview_height,
                      edit_target == 0 ? 400 : 320, edit_target);
}

static void show_splash(void) {
    show_top("romfs:/splash.png", 400, -1);
}

static void put_u32(u8 *p, u32 v) {
    p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = v >> 24;
}

static void show_progress(const char *label, unsigned percent);

static bool fill_texture(u8 *dest, const char *path, int display_w, int image_index) {
    int sw, sh, c;
    stbi_uc *src = load_image(path, &sw, &sh, &c);
    if (!src) return false;
    build_sample_map(&sample_map, sw, sh, display_w, 240, display_w, 240, image_index, false);
    u16 *pixels = (u16 *)dest;
    for (int y = 0; y < TEX_H; ++y) {
        for (int x = 0; x < TEX_W; ++x) {
            int r, g, b;
            int px = x < display_w ? x : display_w - 1;
            int py = y < 240 ? y : 239;
            sample_image(src, sw, &sample_map, px, py, &r, &g, &b);
            unsigned tile = (((y >> 3) * (TEX_W >> 3) + (x >> 3)) << 6)
                + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
            pixels[tile] = (u16)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
        }
        if ((y & 31) == 31)
            show_progress("Images", 50 + (image_index*TEX_H+y+1)*18/(2*TEX_H));
    }
    stbi_image_free(src);
    return true;
}

static bool write_preview_half(u8 *preview, const char *path, int image_index) {
    int sw, sh, channels;
    stbi_uc *src = load_image(path, &sw, &sh, &channels);
    if (!src) return false;
    int width = image_index == 0 ? 412 : 320;
    build_sample_map(&sample_map, sw, sh, width, 240, width, 240, image_index, false);
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 400; ++x) {
            int r = 6, g = 11, b = 39;
            int ix = image_index == 0 ? x + 6 : x - 40;
            if (ix >= 0 && ix < width)
                sample_image(src, sw, &sample_map, ix, y, &r, &g, &b);
            size_t at = ((size_t)(y + image_index * 240) * 400 + x) * 3;
            preview[at] = r; preview[at + 1] = g; preview[at + 2] = b;
        }
        if ((y & 47) == 47)
            show_progress("Apercu", 70 + (image_index*240+y+1)*8/480);
    }
    stbi_image_free(src);
    return true;
}

typedef struct {
    const char *path;
    const u8 *pixels;
    volatile bool done;
    bool okay;
} PngJob;

static void png_worker(void *arg) {
    PngJob *job = arg;
    job->okay = stbi_write_png(job->path, 400, 480, 3, job->pixels, 400*3) != 0;
    job->done = true;
}

static bool write_preview_png(const char *path, const u8 *pixels) {
    PngJob job = {.path = path, .pixels = pixels};
    Thread worker = threadCreate(png_worker, &job, 0x20000, 0x30, -2, false);
    if (!worker) return stbi_write_png(path, 400, 480, 3, pixels, 400*3) != 0;
    u64 start = osGetTime();
    unsigned tick = 0;
    while (!job.done) {
        ui_top_status("Enregistrement apercu", -1,
                      (unsigned)((osGetTime()-start)/1000), tick++);
        present_frame();
        gspWaitForVBlank();
    }
    threadJoin(worker, U64_MAX);
    threadFree(worker);
    return job.okay;
}

static bool write_preview(const char *path) {
    u8 *pixels = malloc(400 * 480 * 3);
    if (!pixels) return false;
    bool ok = write_preview_half(pixels, chosen[0], 0) &&
              write_preview_half(pixels, chosen[1], 1) &&
              write_preview_png(path, pixels);
    free(pixels);
    return ok;
}

static bool write_icon(const char *path) {
    int index = icon_from_bottom ? 1 : 0;
    int sw, sh, channels;
    stbi_uc *src = load_image(chosen[index], &sw, &sh, &channels);
    if (!src) return false;
    u8 *pixels = malloc(48 * 48 * 3);
    if (!pixels) { stbi_image_free(src); return false; }
    build_sample_map(&sample_map, sw, sh, 48, 48, 48, 48, index, false);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            int r, g, b;
            sample_image(src, sw, &sample_map, x, y, &r, &g, &b);
            size_t at = ((size_t)y * 48 + x) * 3;
            pixels[at] = r; pixels[at + 1] = g; pixels[at + 2] = b;
        }
    bool ok = stbi_write_png(path, 48, 48, 3, pixels, 48 * 3) != 0;
    free(pixels);
    stbi_image_free(src);
    return ok;
}

static void smdh_text(u8 *dest, size_t chars, const char *text) {
    for (size_t i = 0; i + 1 < chars && text[i]; ++i) {
        dest[2*i] = (u8)text[i];
        dest[2*i+1] = 0;
    }
}

static bool smdh_icon(u8 *dest, const stbi_uc *src, int sw, int sh,
                      int width, int image_index) {
    build_sample_map(&sample_map, sw, sh, width, width, width, width, image_index, false);
    for (int y = 0; y < width; ++y) for (int x = 0; x < width; ++x) {
        int r, g, b;
        sample_image(src, sw, &sample_map, x, y, &r, &g, &b);
        unsigned tile = (((y >> 3) * (width >> 3) + (x >> 3)) << 6)
            + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
               ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
        u16 rgb = (u16)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
        dest[2*tile] = rgb & 255; dest[2*tile+1] = rgb >> 8;
    }
    return true;
}

static bool write_smdh(const char *path, const char *label) {
    int index = icon_from_bottom ? 1 : 0;
    int sw, sh, channels;
    stbi_uc *src = load_image(chosen[index], &sw, &sh, &channels);
    if (!src) return false;
    u8 *data = calloc(1, 0x36c0);
    if (!data) { stbi_image_free(src); return false; }
    memcpy(data, "SMDH", 4);
    smdh_text(data + 0x8, 0x40, label);
    smdh_text(data + 0x88, 0x80, "Cree avec Shield Theme Maker");
    smdh_text(data + 0x188, 0x40, "Shield-77");
    smdh_icon(data + 0x2040, src, sw, sh, 24, index);
    smdh_icon(data + 0x24c0, src, sw, sh, 48, index);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, 0x36c0, f) == 0x36c0;
    if (f && fclose(f) != 0) ok = false;
    free(data); stbi_image_free(src);
    return ok;
}

static bool write_lz11_literals(FILE *out, const u8 *data, size_t size) {
    u8 header[4] = { 0x11, size & 255, (size >> 8) & 255, (size >> 16) & 255 };
    if (fwrite(header, 1, 4, out) != 4) return false;
    for (size_t p = 0; p < size; p += 8) {
        u8 flag = 0;
        size_t length = size - p < 8 ? size - p : 8;
        if (fwrite(&flag, 1, 1, out) != 1 || fwrite(data + p, 1, length, out) != length) return false;
        if (p && !(p & 0xffff))
            show_progress("Corps du theme", 60 + (unsigned)(p*10/size));
    }
    return fflush(out) == 0;
}

static bool valid_bgm(const char *path) {
    return bcstm_validate(path, NULL);
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
    if (!image_info(chosen[0], &chosen_w[0], &chosen_h[0]) ||
        !image_info(chosen[1], &chosen_w[1], &chosen_h[1])) return false;
    const char *audio_names[] = {"bgm.bcstm", "bgm.ogg", "bgm.mp3"};
    chosen[2][0] = 0;
    for (unsigned i = 0; i < 3; ++i) {
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "sdmc:/3ds/ShieldThemeMaker/input/%s", audio_names[i]);
        if (access(path, R_OK) == 0 &&
            (i != 0 || valid_bgm(path))) {
            strcpy(chosen[2], path); break;
        }
    }
    phase = 3;
    focus_ready[0] = focus_ready[1] = false;
    fit_mode[0] = fit_mode[1] = 0;
    zoom_percent[0] = zoom_percent[1] = 100;
    message[0] = 0;
    return true;
}

static bool copy_bgm(const char *source, const char *dest) {
    FILE *in = fopen(source, "rb");
    if (!in) return false;
    FILE *out = fopen(dest, "wb");
    if (!out) { fclose(in); return false; }
    long total = -1;
    if (fseek(in, 0, SEEK_END) == 0) total = ftell(in);
    bool ok = fseek(in, 0, SEEK_SET) == 0;
    u8 buffer[8192]; size_t n;
    size_t copied = 0;
    u64 last_update = osGetTime();
    while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, n, out) != n) { ok = false; break; }
        copied += n;
        u64 now = osGetTime();
        if (now - last_update >= 250) {
            last_update = now;
            unsigned fraction = total > 0 ? (unsigned)((uint64_t)copied * 4 / (size_t)total) : 0;
            show_progress("Copie audio", 85 + (fraction > 4 ? 4 : fraction));
        }
    }
    if (ferror(in) || fflush(out) != 0) ok = false;
    if (fclose(in) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

static void theme_name(char *out, size_t size) {
    const char *base = custom_theme_name[0] ? custom_theme_name : strrchr(chosen[0], '/');
    base = custom_theme_name[0] ? base : (base ? base + 1 : chosen[0]);
    snprintf(out, size, "%s", custom_theme_name[0] ? "" : "Shield_");
    size_t used = strlen(out);
    for (; *base && (custom_theme_name[0] || *base != '.') &&
           used < size - 1 && used < 28; ++base) {
        unsigned char c = (unsigned char)*base;
        out[used++] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') ? c : '_';
    }
    out[used] = 0;
    if (!used) snprintf(out, size, "Shield_Theme");
}

static void draw_ui(void);
static u64 progress_started;
static void show_progress(const char *label, unsigned percent) {
    if (!progress_started) progress_started = osGetTime();
    snprintf(message, sizeof(message), "%s %u%%", tr(label), percent);
    draw_ui();
    ui_top_status(label, (int)percent, (unsigned)((osGetTime()-progress_started)/1000),
                  (unsigned)(osGetTime()/250));
    present_frame();
}
static void audio_progress(unsigned percent) {
    show_progress("Audio (B annuler)", percent);
}
static void preview_progress(unsigned percent) {
    show_progress("Ecoute (Y/B arreter)", percent);
}

static bool prepare_audio(void) {
    if (!chosen[2][0]) return true;
    if (ends_with(chosen[2], ".bcstm")) return valid_bgm(chosen[2]);
    if (!candidate_path[0]) return false;
    if (candidate_ready && valid_bgm(candidate_path)) return true;
    remove(candidate_path);
    progress_started = 0;
    candidate_ready = convert_audio_to_bcstm(chosen[2], candidate_path,
                      loop_start_ms, loop_end_ms, fade_ms, !custom_loop,
                      audio_quality, audio_progress);
    return candidate_ready;
}

static bool valid_lz_body(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 h[4];
    bool ok = fread(h, 1, 4, f) == 4 && h[0] == 0x11 &&
              ((u32)h[1] | ((u32)h[2] << 8) | ((u32)h[3] << 16)) == BODY_SIZE;
    if (fseek(f, 0, SEEK_END) != 0) ok = false;
    long size = ftell(f);
    if (size != 4 + (long)BODY_SIZE + (BODY_SIZE + 7) / 8) ok = false;
    fclose(f);
    return ok;
}

static bool valid_smdh(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 h[8];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h) && memcmp(h, "SMDH", 4) == 0;
    if (fseek(f, 0, SEEK_END) != 0 || ftell(f) != 0x36c0) ok = false;
    fclose(f);
    return ok;
}

static void clean_stage(const char *folder) {
    const char *names[] = {"body_LZ.bin", "preview.png", "icon.png", "info.smdh", "bgm.bcstm", "bgm.ogg"};
    for (unsigned i = 0; i < 6; ++i) {
        char path[PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s/%s", folder, names[i]) < (int)sizeof(path))
            remove(path);
    }
    rmdir(folder);
}

typedef struct {
    char folder[160], final[160], temporary_zip[192];
    bool zip, music, ogg_preview;
    volatile unsigned stage;
    volatile bool done;
    bool okay;
} ExportJob;

static void export_worker(void *arg) {
    ExportJob *job = arg;
    bool ok;
    if (job->zip) {
        const char *names[] = {"body_LZ.bin", "preview.png", "icon.png", "info.smdh", "bgm.bcstm", "bgm.ogg"};
        size_t count = job->ogg_preview ? 6 : job->music ? 5 : 4;
        job->stage = 92;
        ok = zip_store_files(job->temporary_zip, job->folder, names, count);
        job->stage = 96;
        if (ok) ok = zip_verify(job->temporary_zip, names, count);
        job->stage = 98;
        if (ok) ok = access(job->final, F_OK) != 0 &&
                     rename(job->temporary_zip, job->final) == 0;
        if (!ok) remove(job->temporary_zip);
        job->stage = 99;
        clean_stage(job->folder);
    } else {
        job->stage = 98;
        ok = access(job->final, F_OK) != 0 &&
             rename(job->folder, job->final) == 0;
        if (!ok) clean_stage(job->folder);
    }
    job->okay = ok;
    job->done = true;
}

static bool generate_theme(void) {
    audio_failed_prompt = false;
    progress_started = 0;
    show_progress("Verification", 1);
    int w, h;
    if (!image_info(chosen[0], &w, &h) || !image_info(chosen[1], &w, &h)) {
        strcpy(message, "Image invalide ou trop grande"); return false;
    }
    if (chosen[2][0] && !prepare_audio()) {
        audio_failed_prompt = true;
        strcpy(message, "Audio invalide: sans musique ?");
        return false;
    }
    progress_started = 0;
    aptSetSleepAllowed(false);
    mkdir("sdmc:/themes", 0777);
    DIR *themes = opendir("sdmc:/themes");
    if (!themes) { strcpy(message, "Dossier themes inaccessible"); aptSetSleepAllowed(true); return false; }
    closedir(themes);
    char name[48], folder[160], final[160], other[160];
    char body_path[192], bgm_path[192], ogg_path[192], preview_path[192], icon_path[192], smdh_path[192];
    theme_name(name, sizeof(name));
    int suffix;
    for (suffix = 0; suffix < 10000; ++suffix) {
        char label[64];
        if (suffix) snprintf(label, sizeof(label), "%s_%d", name, suffix);
        else snprintf(label, sizeof(label), "%s", name);
        snprintf(final, sizeof(final), "sdmc:/themes/%s%s", label, export_zip ? ".zip" : "");
        snprintf(other, sizeof(other), "sdmc:/themes/%s%s", label, export_zip ? "" : ".zip");
        if (access(final, F_OK) != 0 && access(other, F_OK) != 0) break;
    }
    if (suffix == 10000) { strcpy(message, "Trop de themes du meme nom"); aptSetSleepAllowed(true); return false; }
    bool created = false;
    for (int attempt = 0; attempt < 100 && !created; ++attempt) {
        snprintf(folder, sizeof(folder), "sdmc:/3ds/ShieldThemeMaker/.stage-%llu-%d",
                 (unsigned long long)osGetTime(), attempt);
        if (access(folder, F_OK) != 0) created = mkdir(folder, 0777) == 0;
    }
    if (!created) { strcpy(message, "Espace temporaire indisponible"); aptSetSleepAllowed(true); return false; }
    show_progress("Preparation", 50);
    snprintf(body_path, sizeof(body_path), "%s/body_LZ.bin", folder);
    snprintf(bgm_path, sizeof(bgm_path), "%s/bgm.bcstm", folder);
    snprintf(ogg_path, sizeof(ogg_path), "%s/bgm.ogg", folder);
    snprintf(preview_path, sizeof(preview_path), "%s/preview.png", folder);
    snprintf(icon_path, sizeof(icon_path), "%s/icon.png", folder);
    snprintf(smdh_path, sizeof(smdh_path), "%s/info.smdh", folder);
    u8 *body = calloc(1, BODY_SIZE);
    if (!body) { clean_stage(folder); strcpy(message, "Memoire insuffisante"); aptSetSleepAllowed(true); return false; }
    put_u32(body, 1);
    body[5] = chosen[2][0] ? 1 : 0;
    put_u32(body + 0x0c, 3); put_u32(body + 0x10, 1); put_u32(body + 0x18, BODY_HEADER);
    put_u32(body + 0x20, 3); put_u32(body + 0x24, 1); put_u32(body + 0x28, BODY_HEADER + TEX_BYTES);
    bool ok = fill_texture(body + BODY_HEADER, chosen[0], 412, 0)
           && fill_texture(body + BODY_HEADER + TEX_BYTES, chosen[1], 320, 1);
    if (ok) show_progress("Images", 60);
    if (ok) {
        FILE *out = fopen(body_path, "wb");
        ok = out && write_lz11_literals(out, body, BODY_SIZE);
        if (out && fclose(out) != 0) ok = false;
    }
    free(body);
    if (ok) ok = valid_lz_body(body_path);
    if (ok) show_progress("Corps du theme", 70);
    if (ok) ok = write_preview(preview_path);
    if (ok) show_progress("Apercu", 78);
    if (ok) ok = write_icon(icon_path);
    if (ok) ok = write_smdh(smdh_path, name) && valid_smdh(smdh_path);
    if (ok) show_progress("Icone", 84);
    if (ok) {
        int pw, ph, iw, ih, c;
        ok = stbi_info(preview_path, &pw, &ph, &c) && pw == 400 && ph == 480 &&
             stbi_info(icon_path, &iw, &ih, &c) && iw == 48 && ih == 48;
    }
    if (ok && chosen[2][0]) {
        const char *source = ends_with(chosen[2], ".bcstm") ? chosen[2] : candidate_path;
        ok = copy_bgm(source, bgm_path) && valid_bgm(bgm_path);
        if (ok && ends_with(chosen[2], ".ogg")) {
            AudioInfo preview_info;
            ok = copy_bgm(chosen[2], ogg_path) && audio_probe(ogg_path, &preview_info);
        }
        audio_failed_prompt = !ok;
    }
    if (ok) show_progress("Controle", 90);
    if (!ok) {
        clean_stage(folder);
        strcpy(message, audio_failed_prompt ? "Audio invalide: sans musique ?" :
                "Export echoue (SD / memoire)");
        aptSetSleepAllowed(true);
        return false;
    }
    ExportJob job = {.zip = export_zip, .music = chosen[2][0] != 0,
                     .ogg_preview = ends_with(chosen[2], ".ogg"), .stage = 91};
    snprintf(job.folder, sizeof(job.folder), "%s", folder);
    snprintf(job.final, sizeof(job.final), "%s", final);
    snprintf(job.temporary_zip, sizeof(job.temporary_zip), "%s.zip", folder);
    Thread worker = threadCreate(export_worker, &job, 0x10000, 0x30, -2, false);
    if (worker) {
        u64 last_update = 0;
        while (!job.done) {
            u64 now = osGetTime();
            if (now - last_update >= 250) {
                show_progress(job.stage == 92 ? "Ecriture ZIP" :
                              job.stage == 96 ? "Verification ZIP" :
                              job.stage == 99 ? "Nettoyage" : "Finalisation",
                              job.stage);
                last_update = now;
            }
            gspWaitForVBlank();
        }
        threadJoin(worker, U64_MAX);
        threadFree(worker);
    } else export_worker(&job);
    ok = job.okay;
    if (!ok) { strcpy(message, "Export final impossible, themes intacts"); aptSetSleepAllowed(true); return false; }
    show_progress("Termine", 100);
    snprintf(message, sizeof(message), "%s: themes/%s",
             locale_is_french() ? "Cree" : "Saved", strrchr(final, '/') + 1);
    aptSetSleepAllowed(true);
    return true;
}

static const char *short_name(const char *path) {
    const char *p = strrchr(path, '/'); return p ? p + 1 : path;
}

static bool crop_map_geometry(int index, int *x, int *y, int *w, int *h,
                              int *visible_w, int *visible_h) {
    int sw = chosen_w[index], sh = chosen_h[index];
    if (sw <= 0 || sh <= 0) return false;
    if ((int64_t)sw * 50 > (int64_t)sh * 80) {
        *w = 80;
        *h = (int)((int64_t)sh * 80 / sw);
    } else {
        *h = 50;
        *w = (int)((int64_t)sw * 50 / sh);
    }
    if (*w < 2) *w = 2;
    if (*h < 2) *h = 2;
    *x = 225 + (80-*w)/2;
    *y = 132 + (50-*h)/2;
    int dw = index == 0 ? 412 : 320;
    int64_t sx = ((int64_t)sw << 16)/dw, sy = ((int64_t)sh << 16)/240;
    int64_t scale = fit_mode[index] == 1 ? (sx > sy ? sx : sy) :
                    (sx < sy ? sx : sy) * 100 / zoom_percent[index];
    *visible_w = (int)((dw * scale) >> 16);
    *visible_h = (int)((240 * scale) >> 16);
    return true;
}

static void draw_crop_map(int index) {
    int x, y, w, h, visible_w, visible_h;
    if (!crop_map_geometry(index, &x, &y, &w, &h, &visible_w, &visible_h)) return;
    ui_box(221, 127, 88, 60, 8, 38, 69);
    ui_box(x-1, y-1, w+2, h+2, 90, 143, 170);
    ui_box(x, y, w, h, 17, 54, 77);
    if (fit_mode[index] == 2) {
        int cx = x + (w-1)*focus_x[index]/100;
        int cy = y + (h-1)*focus_y[index]/100;
        ui_box(cx-2, cy, 5, 1, 255, 194, 44);
        ui_box(cx, cy-2, 1, 5, 255, 194, 44);
        return;
    }
    int sw = chosen_w[index], sh = chosen_h[index];
    int cx = visible_w < sw ? x + (int64_t)(sw-visible_w)*focus_x[index]*w/(100*sw) : x;
    int cy = visible_h < sh ? y + (int64_t)(sh-visible_h)*focus_y[index]*h/(100*sh) : y;
    int cw = visible_w < sw ? (int64_t)visible_w*w/sw : w;
    int ch = visible_h < sh ? (int64_t)visible_h*h/sh : h;
    if (cw < 2) cw = 2;
    if (ch < 2) ch = 2;
    if (cx+cw > x+w) cx = x+w-cw;
    if (cy+ch > y+h) cy = y+h-ch;
    u8 r = index ? 255 : 24, g = index ? 194 : 211, b = index ? 44 : 244;
    ui_box(cx, cy, cw, 2, r, g, b);
    ui_box(cx, cy+ch-2, cw, 2, r, g, b);
    ui_box(cx, cy, 2, ch, r, g, b);
    ui_box(cx+cw-2, cy, 2, ch, r, g, b);
}

static void draw_ui(void) {
    const char *step = phase == 0 ? "1/7  IMAGE DU HAUT" :
                       phase == 1 ? "2/7  IMAGE DU BAS" :
                       phase == 2 ? "3/7  MUSIQUE (OPTION)" :
                       phase == 3 ? "4/7  CADRAGE HAUT" :
                       phase == 4 ? "4/7  CADRAGE BAS" :
                       phase == 5 ? "5/7  BOUCLE AUDIO" :
                       phase == 6 ? "6/7  FORMAT DE SORTIE" :
                       phase == 7 ? "7/7  VERIFICATION" :
                       phase == 9 ? "AUDIO NON VALIDE" : "THEME TERMINE";
    ui_begin("SHIELD THEME MAKER v0.2.11", step);
    ui_box(7, 58, 306, 130, 10, 27, 53);
    if (phase < 3) {
        char line[64];
        snprintf(line, sizeof(line), tr("Haut: %.23s"), chosen[0][0] ? short_name(chosen[0]) : tr("a choisir"));
        ui_text(13, 58, line, 245, 248, 252);
        snprintf(line, sizeof(line), tr("Bas:  %.23s"), chosen[1][0] ? short_name(chosen[1]) : tr("a choisir"));
        ui_text(13, 75, line, 245, 248, 252);
        snprintf(line, sizeof(line), "%.30s", cwd);
        ui_text(13, 92, line, 132, 218, 242);
        for (int i = scroll; i < count && i < scroll + 5; ++i) {
            int y = 110 + (i - scroll) * 16;
            if (i == cursor) ui_box(11, y-1, 298, 17, 16, 96, 130);
            snprintf(line, sizeof(line), "%c %.27s", entries[i].directory ? '+' : ' ', entries[i].name);
            ui_text(16, y, line, i == cursor ? 255 : 213, 245, 250);
        }
        if (!count) ui_text(13, 128, "Aucun fichier compatible", 235, 220, 194);
        if (date_done < date_total) {
            snprintf(line, sizeof(line), "%s %d/%d", tr("Tri par date"), date_done, date_total);
            ui_text(13, 190, line, 255, 213, 146);
        } else if (scan_truncated) ui_text(13, 190, "Liste limitee a 512 fichiers", 255, 213, 146);
        ui_footer(phase == 2 ?
            (locale_is_french() ? "Y extrait A choix X sans B" : "Y sample A pick X none B") :
            phase == 0 ?
            (locale_is_french() ? "Y voir A HAUT B retour" : "Y view A TOP B back") :
            (locale_is_french() ? "Y voir A BAS B retour" : "Y view A BOTTOM B back"));
    } else if (phase == 3 || phase == 4) {
        int i = phase - 3;
        char line[48];
        ui_text(13, 61, i ? "IMAGE DU BAS (bord jaune)" : "IMAGE DU HAUT (bord cyan)", 255, 255, 255);
        snprintf(line, sizeof(line), "%.29s", short_name(chosen[i]));
        ui_text(13, 80, line, 151, 221, 244);
        snprintf(line, sizeof(line), tr("Zoom: %d%%  %s"), zoom_percent[i],
                 tr(fit_mode[i] == 1 ? "ENTIERE" :
                    fit_mode[i] == 2 ? "PROLONGER" : "REMPLIR"));
        ui_text(13, 105, line, 255, 255, 255);
        snprintf(line, sizeof(line), tr("Centre: %d%% / %d%%"), focus_x[i], focus_y[i]);
        ui_text(13, 124, line, 255, 255, 255);
        ui_text(13, 151, tr("X: CHANGER LE CADRAGE"), 24, 211, 244);
        ui_text(13, 169, tr("CROIX bouger  L/R zoom"), 196, 224, 238);
        draw_crop_map(i);
        ui_text(13, 190, tr(fit_mode[i] == 1 ? "Image entiere avec bandes" :
                            fit_mode[i] == 2 ? "Repete les pixels du bord" :
                                               "Coupe les bords pour remplir"),
                255, 213, 146);
        ui_footer(tr("Y auto  A suite  B retour"));
    } else if (phase == 5) {
        char line[48];
        unsigned ms = audio_info.duration_ms;
        AudioProfile profile = audio_profile(audio_quality);
        snprintf(line, sizeof(line), locale_is_french() ?
                 "< > Qualite: %lu kHz / %lu:%02lu" :
                 "< > Quality: %lu kHz / %lu:%02lu",
                 (unsigned long)(profile.rate/1000),
                 (unsigned long)(profile.max_ms/60000),
                 (unsigned long)(profile.max_ms/1000%60));
        ui_text(13, 61, line, 24, 211, 244);
        snprintf(line, sizeof(line), "Source %s%u:%02u  X: %s",
                 audio_info.approximate ? "~" : "", ms/60000, ms/1000%60,
                 custom_loop ? (locale_is_french() ? "PERSO" : "CUSTOM") : "AUTO");
        ui_text(13, 79, line, 245, 248, 252);
        snprintf(line, sizeof(line), tr("Debut: %lu:%02lu"),
                 (unsigned long)(loop_start_ms/60000),
                 (unsigned long)(loop_start_ms/1000%60));
        ui_text(13, 99, line, 245, 248, 252);
        snprintf(line, sizeof(line), custom_loop ?
                 (locale_is_french() ? "Fin: %lu:%02lu" : "End: %lu:%02lu") :
                 (locale_is_french() ? "Fin maxi: %lu:%02lu" : "Max end: %lu:%02lu"),
                 (unsigned long)(loop_end_ms/60000),
                 (unsigned long)(loop_end_ms/1000%60));
        ui_text(13, 118, line, 245, 248, 252);
        snprintf(line, sizeof(line), locale_is_french() ?
                 "L/R fondu: %lu,%lu s" : "L/R fade: %lu.%lu s",
                 (unsigned long)(fade_ms / 1000),
                 (unsigned long)(fade_ms % 1000 / 100));
        ui_text(13, 137, line, 245, 248, 252);
        ui_box(18, 159, 280, 6, 18, 57, 78);
        ui_box(18, 177, 280, 6, 18, 57, 78);
        ui_text(7, 154, locale_is_french() ? "D" : "S", 24, 211, 244);
        ui_text(7, 172, locale_is_french() ? "F" : "E", 255, 194, 44);
        if (audio_info.duration_ms) {
            int sx = 18 + (uint64_t)loop_start_ms * 277 / audio_info.duration_ms;
            int ex = 18 + (uint64_t)loop_end_ms * 277 / audio_info.duration_ms;
            ui_box(sx, 155, 4, 14, 24, 211, 244);
            ui_box(ex, 173, 4, 14, 255, 194, 44);
        }
        ui_footer(locale_is_french() ? "X mode Y lire A suite B retour" :
                                      "X mode Y play A next B back");
    } else if (phase == 6) {
        ui_text(13, 65, "FORMAT POUR ANEMONE3DS", 245, 248, 252);
        ui_text(13, 95, export_zip ? "> ARCHIVE ZIP" : "> DOSSIER CLASSIQUE", 24, 211, 244);
        ui_text(13, 123, icon_from_bottom ? "Icone: image du BAS" : "Icone: image du HAUT", 245, 248, 252);
        ui_text(13, 166, "Aucun theme existant ecrase", 196, 224, 238);
        ui_footer("X format  Y icone  A suite  B");
    } else if (phase == 7) {
        char line[48];
        snprintf(line, sizeof(line), tr("Haut: %.23s"), short_name(chosen[0])); ui_text(13, 66, line, 245, 248, 252);
        snprintf(line, sizeof(line), tr("Bas:  %.23s"), short_name(chosen[1])); ui_text(13, 87, line, 245, 248, 252);
        snprintf(line, sizeof(line), tr("Audio: %.22s"), chosen[2][0] ? short_name(chosen[2]) : tr("sans musique"));
        ui_text(13, 108, line, 245, 248, 252);
        ui_text(13, 139, export_zip ? "Sortie: archive ZIP" : "Sortie: dossier", 24, 211, 244);
        char label[48]; theme_name(label, sizeof(label));
        snprintf(line, sizeof(line), "Nom: %.26s", label);
        ui_text(13, 163, line, 245, 248, 252);
        ui_footer("Y nom  A creer  B modifier");
    } else if (phase == 9) {
        ui_text(13, 74, "Musique non valide.", 255, 213, 146);
        ui_text(13, 101, "Ne pas exporter cet audio.", 255, 255, 255);
        ui_text(13, 137, "A: creer sans musique", 24, 211, 244);
        ui_text(13, 158, "B: annuler / corriger", 245, 248, 252);
        ui_footer("START quitter");
    } else {
        ui_text(13, 90, "Theme cree dans /themes", 24, 211, 244);
        ui_footer("A nouveau theme  START quitter");
    }
    if (message[0] && phase != 9) {
        ui_box(8, 190, 304, 17, 21, 47, 74);
        ui_text(11, 190, message, 255, 235, 176);
    }
}

static bool write_setting(const char *path, int value) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool okay = fputc(value, f) != EOF;
    if (fclose(f) != 0) okay = false;
    return okay;
}

static bool read_setting(const char *path, int *value) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    *value = fgetc(f);
    fclose(f);
    return true;
}

static bool load_language(void) {
    int saved;
    if (read_setting(LANGUAGE_PATH, &saved) && (saved == 'F' || saved == 'E')) {
        locale_set_french(saved == 'F');
        return true;
    }
    bool french = false;
    if (R_SUCCEEDED(cfguInit())) {
        u8 system_language = CFG_LANGUAGE_EN;
        if (R_SUCCEEDED(CFGU_GetSystemLanguage(&system_language)))
            french = system_language == CFG_LANGUAGE_FR;
        cfguExit();
    }
    locale_set_french(french);
    return false;
}

static void draw_language_prompt(void) {
    ui_begin("SHIELD THEME MAKER", "LANGUE / LANGUAGE");
    ui_box(8, 60, 304, 142, 10, 27, 53);
    ui_text(14, 70, locale_is_french() ? "LANGUE DETECTEE" : "DETECTED LANGUAGE", 255, 255, 255);
    ui_text(14, 98, locale_is_french() ? "FRANCAIS" : "ENGLISH", 24, 211, 244);
    ui_text(14, 130, locale_is_french() ? "A: confirmer" : "A: confirm", 255, 255, 255);
    ui_text(14, 153, locale_is_french() ? "B: changer de langue" : "B: change language", 255, 255, 255);
    ui_footer(locale_is_french() ? "START quitter" : "START exit");
    present_frame();
}

static bool confirm_language(void) {
    draw_language_prompt();
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) return false;
        if (keys & KEY_B) { locale_set_french(!locale_is_french()); draw_language_prompt(); }
        if (keys & KEY_A) {
            write_setting(LANGUAGE_PATH, locale_is_french() ? 'F' : 'E');
            return true;
        }
        gspWaitForVBlank();
    }
    return false;
}

static void draw_guide(unsigned page, bool mandatory) {
    static const char *fr_title[GUIDE_PAGES] = {
        "BIENVENUE", "FICHIERS", "CADRAGE", "3 MODES",
        "MUSIQUE", "BOUCLE AUDIO", "SORTIE", "CREATION"
    };
    static const char *en_title[GUIDE_PAGES] = {
        "WELCOME", "FILES", "CROPPING", "3 MODES",
        "MUSIC", "AUDIO LOOP", "OUTPUT", "CREATE"
    };
    static const char *fr[GUIDE_PAGES][7] = {
        {"Cree un theme sur ta 3DS.", "Il faut 2 images: haut et bas.",
         "La musique est facultative.", "Rien ne s'installe tout seul.",
         "A: avancer; B: revenir.", "START: quitter l'app.",
         "SELECT: revoir ce guide."},
        {"Images a la racine ou dans", "un dossier: les 2 marchent.",
         "Haut/Bas: choisir une ligne.", "A: ouvrir dossier ou choisir.",
         "Y: voir l'image sur l'ecran", "du haut avant de choisir.",
         "D'abord HAUT, puis BAS."},
        {"Le haut a un bord CYAN.", "Le bas a un bord JAUNE.",
         "Regarde l'image en haut.", "Croix: deplacer le cadrage.",
         "L: reculer; R: rapprocher.", "Y: centrer auto si tu veux.",
         "A: valider ce cadrage."},
        {"X change la facon de cadrer.", "REMPLIR: couvre tout l'ecran;",
         "les bords peuvent etre coupes.", "ENTIERE: garde toute l'image;",
         "des bandes peuvent apparaitre.", "PROLONGER: repete les bords.",
         "Choisis ce qui te plait."},
        {"Tu peux creer sans musique.", "X: passer cette etape.",
         "Sinon: MP3, OGG ou BCSTM.", "Y: extrait si lisible (20 s).",
         "A: choisir cette musique.", "Meme si elle dure 7 minutes,",
         "tu choisis juste un passage."},
        {"Gauche/Droite: qualite audio.", "Plus net = passage plus court.",
         "Toucher D/F: debut et fin.", "X: auto ou choix manuel.",
         "L/R: raccord fin-debut.", "Y: ecouter en boucle.",
         "Y/B stop; B encore: retour."},
        {"X: ZIP ou dossier classique.", "Les deux vont dans /themes/.",
         "Y: icone = image haut/bas.", "L'icone aide a retrouver",
         "le theme dans Anemone.", "Puis Y peut nommer le theme.",
         "A: voir le resume."},
        {"Relis haut, bas et musique.", "B: corriger avant de creer.",
         "A: creer; suis la barre.", "Garde la console eveillee.",
         "Le theme reste dans /themes/.", "Applique-le avec Anemone.",
         "Les anciens themes restent."}
    };
    static const char *en[GUIDE_PAGES][7] = {
        {"Make a theme on your 3DS.", "Pick 2 images: top and bottom.",
         "Music is optional.", "Nothing installs by itself.",
         "A: next; B: go back.", "START: exit the app.",
         "SELECT: reopen this guide."},
        {"Images can be in SD root or", "inside any folder.",
         "Up/Down: choose a file.", "A: open folder or select.",
         "Y: see the image on top", "before you select it.",
         "First TOP, then BOTTOM."},
        {"TOP image has a CYAN border.", "BOTTOM has a YELLOW border.",
         "Watch the top screen.", "D-pad: move the crop.",
         "L: zoom out; R: zoom in.", "Y: optional auto center.",
         "A: keep this crop."},
        {"X changes the crop mode.", "FILL: fills the screen;",
         "the edges may be cut.", "WHOLE: keeps all the image;",
         "bars may appear.", "EXTEND: repeats edge pixels.",
         "Choose the look you like."},
        {"Music is optional.", "X: skip music.",
         "Or pick MP3, OGG or BCSTM.", "Y: hear 20 s if supported.",
         "A: choose that file.", "Even if it lasts 7 minutes,",
         "you use only one passage."},
        {"Left/Right: audio quality.", "Clearer sound = shorter clip.",
         "Touch S/E: start and end.", "X: automatic or custom.",
         "L/R: join the end to start.", "Y: listen in a loop.",
         "Y/B stop; B again: back."},
        {"X: ZIP or folder output.", "Both go into /themes/.",
         "Y: icon from TOP or BOTTOM.", "The icon helps you find it",
         "in Anemone.", "Next step: Y names the theme.",
         "A: review your choices."},
        {"Review top, bottom, music.", "B: go back and fix a choice.",
         "A: create; watch progress.", "Keep your 3DS awake.",
         "Theme stays in /themes/.", "Apply it with Anemone.",
         "Existing themes stay safe."}
    };
    bool french = locale_is_french();
    char heading[32];
    snprintf(heading, sizeof(heading), "GUIDE %u/%u - %s", page + 1,
             GUIDE_PAGES, french ? fr_title[page] : en_title[page]);
    ui_begin("SHIELD THEME MAKER", heading);
    ui_box(8, 60, 304, 145, 10, 27, 53);
    for (unsigned i = 0; i < 7; ++i) {
        const char *line = french ? fr[page][i] : en[page][i];
        bool cyan = i == 0 || (page == 3 && (i == 1 || i == 3 || i == 5));
        bool yellow = page == 2 && i == 1;
        ui_text(13, 64 + i*19, line,
                yellow ? 255 : cyan ? 101 : 245,
                yellow ? 194 : cyan ? 215 : 248,
                yellow ? 44 : cyan ? 241 : 252);
    }
    ui_box(13, 200, 294, 3, 18, 57, 78);
    ui_box(13, 200, (page + 1) * 294 / GUIDE_PAGES, 3, 24, 211, 244);
    if (!page && mandatory)
        ui_footer(french ? "A continuer  START quitter" : "A continue  START exit");
    else if (page + 1 == GUIDE_PAGES)
        ui_footer(french ? "A commencer  B precedent" : "A start  B previous");
    else
        ui_footer(french ? "A suite  B precedent  START" :
                           "A next  B previous  START");
    present_frame();
}

static bool show_guide(bool mandatory) {
    unsigned page = 0;
    draw_guide(page, mandatory);
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) return false;
        if (keys & KEY_B) {
            if (page) { --page; draw_guide(page, mandatory); }
            else if (!mandatory) return true;
        }
        if (keys & KEY_A) {
            if (page + 1 < GUIDE_PAGES) { ++page; draw_guide(page, mandatory); }
            else { write_setting(GUIDE_PATH, GUIDE_VERSION); return true; }
        }
        gspWaitForVBlank();
    }
    return false;
}

static void draw_language_settings(int selected) {
    ui_begin("SHIELD THEME MAKER", "LANGUE / LANGUAGE");
    ui_box(8, 60, 304, 143, 10, 27, 53);
    if (selected == 0) ui_box(11, 85, 298, 27, 16, 96, 130);
    if (selected == 1) ui_box(11, 123, 298, 27, 16, 96, 130);
    ui_text(17, 89, "FRANCAIS", 245, 248, 252);
    ui_text(17, 127, "ENGLISH", 245, 248, 252);
    ui_footer(locale_is_french() ? "HAUT/BAS choisir  A valider  B" :
                                   "UP/DOWN choose  A confirm  B");
    present_frame();
}

static bool show_language_settings(void) {
    int selected = locale_is_french() ? 0 : 1;
    draw_language_settings(selected);
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) return false;
        if (keys & (KEY_B | KEY_SELECT)) return true;
        if (keys & (KEY_UP | KEY_DOWN)) { selected ^= 1; draw_language_settings(selected); }
        if (keys & KEY_A) {
            locale_set_french(selected == 0);
            write_setting(LANGUAGE_PATH, selected == 0 ? 'F' : 'E');
            return true;
        }
        gspWaitForVBlank();
    }
    return false;
}

static void draw_options(int selected) {
    ui_begin("SHIELD THEME MAKER", "OPTIONS");
    ui_box(8, 60, 304, 143, 10, 27, 53);
    if (selected == 0) ui_box(11, 85, 298, 27, 16, 96, 130);
    if (selected == 1) ui_box(11, 123, 298, 27, 16, 96, 130);
    ui_text(17, 89, locale_is_french() ? "LANGUE" : "LANGUAGE", 245, 248, 252);
    ui_text(17, 127, "GUIDE", 245, 248, 252);
    ui_footer(locale_is_french() ? "HAUT/BAS choisir  A ouvrir  B" :
                                   "UP/DOWN choose  A open  B");
    present_frame();
}

static bool show_options(void) {
    int selected = 0;
    draw_options(selected);
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) return false;
        if (keys & (KEY_B | KEY_SELECT)) return true;
        if (keys & (KEY_UP | KEY_DOWN)) { selected ^= 1; draw_options(selected); }
        if (keys & KEY_A) {
            if (selected == 0) {
                if (!show_language_settings()) return false;
            } else if (!show_guide(false)) return false;
            draw_options(selected);
        }
        gspWaitForVBlank();
    }
    return false;
}

static u32 repeat_browser_direction(u32 pressed, u32 held) {
    static u32 previous;
    static u64 started, repeated;
    u32 direction = held & (KEY_UP | KEY_DOWN);
    if (direction == (KEY_UP | KEY_DOWN)) direction = 0;
    u64 now = osGetTime();
    if (direction != previous) {
        previous = direction;
        started = repeated = now;
        return pressed;
    }
    if (direction && now - started >= 600) {
        u64 delay = now - started >= 1500 ? 45 : 135;
        if (now - repeated >= delay) {
            repeated = now;
            return pressed | direction;
        }
    }
    return pressed;
}

static u32 repeat_crop_direction(u32 pressed, u32 held) {
    static u32 previous;
    static u64 started, repeated;
    u32 direction = held & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if (direction && (direction & (direction - 1))) direction = 0;
    u64 now = osGetTime();
    if (direction != previous) {
        previous = direction;
        started = repeated = now;
        return pressed;
    }
    if (direction && now - started >= 500 && now - repeated >= 110) {
        repeated = now;
        return pressed | direction;
    }
    return pressed;
}

int main(void) {
    gfxInitDefault();
    if (R_SUCCEEDED(APT_CheckNew3DS(&new3ds)) && new3ds) osSetSpeedupEnable(true);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    if (romfsInit() != 0 || !ui_init()) {
        gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
        consoleInit(GFX_BOTTOM, NULL);
        printf("Ressources de l'application indisponibles.\nSTART pour quitter.");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); }
        ui_shutdown(); romfsExit(); gfxExit(); return 1;
    }
    show_splash();
    DIR *sd = opendir("sdmc:/");
    if (!sd) {
        ui_begin("SHIELD THEME MAKER", "CARTE SD ABSENTE");
        ui_text(12, 90, "Carte SD indisponible", 255, 255, 255);
        ui_footer("START quitter");
        present_frame();
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); }
        ui_shutdown(); romfsExit(); gfxExit(); return 1;
    }
    closedir(sd);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/ShieldThemeMaker", 0777);
    mkdir("sdmc:/3ds/ShieldThemeMaker/input", 0777);
    bool language_saved = load_language();
    int guide_seen = 0;
    if ((!language_saved && !confirm_language()) ||
        ((!read_setting(GUIDE_PATH, &guide_seen) || guide_seen != GUIDE_VERSION) && !show_guide(true))) {
        ui_shutdown(); romfsExit(); gfxExit(); return 0;
    }
    bool candidate_available = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        snprintf(candidate_path, sizeof(candidate_path),
                 "sdmc:/3ds/ShieldThemeMaker/.preview-%llu-%d.bcstm",
                 (unsigned long long)osGetTime(), attempt);
        if (access(candidate_path, F_OK) != 0) { candidate_available = true; break; }
    }
    if (!candidate_available) candidate_path[0] = 0;
    scan_directory(); draw_ui(); present_frame();
    while (aptMainLoop()) {
        hidScanInput(); u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_SELECT) {
            if (!show_options()) break;
            draw_ui(); present_frame();
            continue;
        }
        bool redraw = false;
        int previous_phase = phase;
        if (phase < 3) {
            keys = repeat_browser_direction(keys, hidKeysHeld());
            if ((keys & KEY_UP) && cursor > 0) { cursor--; if (cursor < scroll) scroll = cursor; redraw = true; }
            if ((keys & KEY_DOWN) && cursor + 1 < count) { cursor++; if (cursor >= scroll + 5) scroll = cursor - 4; redraw = true; }
            if (keys & KEY_B) { parent_directory(); redraw = true; }
            if ((keys & KEY_X) && phase == 0) {
                if (automatic_pair() && chosen[2][0] && !ends_with(chosen[2], ".bcstm")) {
                    if (!audio_probe(chosen[2], &audio_info)) chosen[2][0] = 0;
                    else {
                        loop_start_ms = 0;
                        loop_end_ms = audio_info.duration_ms < audio_max_ms() ? audio_info.duration_ms : audio_max_ms();
                        fade_ms = 500; candidate_ready = false;
                    }
                }
                redraw = true;
            }
            if ((keys & KEY_X) && phase == 2) { chosen[2][0] = 0; phase = 3; redraw = true; }
            if ((keys & KEY_Y) && phase < 2 && count && !entries[cursor].directory) {
                char path[PATH_SIZE]; int width, height;
                if (full_path(path, sizeof(path), entries[cursor].name) &&
                    image_info(path, &width, &height) && show_top(path, phase == 0 ? 400 : 320, phase))
                    strcpy(message, "A pour choisir cette image");
                else strcpy(message, "Apercu impossible");
                redraw = true;
            }
            if ((keys & KEY_Y) && phase == 2 && count && !entries[cursor].directory) {
                char path[PATH_SIZE];
                AudioInfo sample_info;
                if (!full_path(path, sizeof(path), entries[cursor].name)) {
                    strcpy(message, "Chemin trop long");
                } else if (ends_with(path, ".bcstm")) {
                    progress_started = 0;
                    strcpy(message, "Extrait BCSTM en lecture...");
                    draw_ui(); present_frame();
                    bool cancelled = false;
                    bool played = audio_preview_bcstm(path, preview_progress, &cancelled);
                    strcpy(message, cancelled ? "Ecoute arretee" :
                           played ? "Fin de l'extrait (A pour choisir)" :
                                    "Apercu BCSTM indisponible");
                } else if (!audio_probe(path, &sample_info)) {
                    strcpy(message, "Audio illisible ou trop court");
                } else {
                    uint32_t end = sample_info.duration_ms < 20000 ? sample_info.duration_ms : 20000;
                    progress_started = 0;
                    strcpy(message, "Extrait audio en lecture...");
                    draw_ui(); present_frame();
                    bool cancelled = false;
                    bool played = audio_preview_source(path, 0, end, 0, false,
                                                       audio_quality,
                                                       preview_progress, &cancelled);
                    strcpy(message, cancelled ? "Ecoute arretee" :
                           played ? "Fin de l'extrait (A pour choisir)" : "Ecoute indisponible");
                }
                redraw = true;
            }
            if ((keys & KEY_A) && count) {
                if (entries[cursor].directory) enter_directory(entries[cursor].name);
                else {
                    char path[PATH_SIZE]; int width, height;
                    if (!full_path(path, sizeof(path), entries[cursor].name)) strcpy(message, "Chemin trop long");
                    else if (phase < 2 && !image_info(path, &width, &height))
                        strcpy(message, "Image invalide ou trop grande");
                    else if (phase == 2 && ends_with(path, ".bcstm") && !valid_bgm(path))
                        strcpy(message, "BCSTM invalide ou trop grand");
                    else if (phase == 2 && !ends_with(path, ".bcstm") && !audio_probe(path, &audio_info))
                        strcpy(message, "MP3/OGG illisible ou trop court");
                    else {
                        if (phase < 2) {
                            chosen_w[phase] = width;
                            chosen_h[phase] = height;
                            focus_ready[phase] = false;
                            fit_mode[phase] = 0;
                            zoom_percent[phase] = 100;
                            focus_x[phase] = focus_y[phase] = 50;
                        }
                        strcpy(chosen[phase], path);
                        if (phase == 2 && !ends_with(path, ".bcstm")) {
                            loop_start_ms = 0;
                            loop_end_ms = audio_info.duration_ms < audio_max_ms() ? audio_info.duration_ms : audio_max_ms();
                            fade_ms = 500; custom_loop = false; candidate_ready = false;
                        }
                        int old_phase = phase++;
                        if (old_phase == 1) scan_directory();
                        else if (old_phase == 0) cursor = scroll = 0;
                        message[0] = 0;
                    }
                }
                redraw = true;
            }
        } else if (phase == 3 || phase == 4) {
            keys = repeat_crop_direction(keys, hidKeysHeld());
            if (keys & KEY_B) {
                clear_preview();
                if (phase == 3) { phase = 2; scan_directory(); }
                else phase = 3;
                redraw = true;
            } else {
                int i = phase - 3;
                if (keys & KEY_X) {
                    fit_mode[i] = (fit_mode[i] + 1) % 3;
                    zoom_percent[i] = fit_mode[i] == 1 ? whole_image_zoom(i) : 100;
                    redraw = true;
                }
                if (keys & KEY_Y) { focus_ready[i] = false; redraw = true; }
                if (keys & KEY_L) {
                    int whole = whole_image_zoom(i);
                    if (fit_mode[i] != 1) {
                        int step = zoom_percent[i] <= 50 ? 5 : 10;
                        zoom_percent[i] = zoom_percent[i] > whole + step ?
                                          zoom_percent[i] - step : whole;
                        if (zoom_percent[i] <= whole) fit_mode[i] = 1;
                    }
                    redraw = true;
                }
                if (keys & KEY_R) {
                    if (fit_mode[i] == 1) {
                        fit_mode[i] = 0;
                        zoom_percent[i] = whole_image_zoom(i);
                    }
                    int step = zoom_percent[i] < 50 ? 5 : 10;
                    if (zoom_percent[i] < 250) zoom_percent[i] += step;
                    if (zoom_percent[i] > 250) zoom_percent[i] = 250;
                    redraw = true;
                }
                int old_x = focus_x[i], old_y = focus_y[i];
                if (keys & KEY_LEFT) focus_x[i] -= 5;
                if (keys & KEY_RIGHT) focus_x[i] += 5;
                if (keys & KEY_UP) focus_y[i] -= 5;
                if (keys & KEY_DOWN) focus_y[i] += 5;
                if (hidKeysHeld() & KEY_TOUCH) {
                    touchPosition touch;
                    hidTouchRead(&touch);
                    int mx, my, mw, mh, vw, vh;
                    if (crop_map_geometry(i, &mx, &my, &mw, &mh, &vw, &vh) &&
                        touch.px >= mx && touch.px < mx+mw &&
                        touch.py >= my && touch.py < my+mh) {
                        if (fit_mode[i] == 2) {
                            focus_x[i] = clamp_focus((touch.px-mx)*100/mw);
                            focus_y[i] = clamp_focus((touch.py-my)*100/mh);
                        } else if (vw < chosen_w[i]) {
                            int target = (int64_t)(touch.px-mx)*chosen_w[i]/mw;
                            focus_x[i] = clamp_focus((target-vw/2)*100/(chosen_w[i]-vw));
                        }
                        if (fit_mode[i] != 2 && vh < chosen_h[i]) {
                            int target = (int64_t)(touch.py-my)*chosen_h[i]/mh;
                            focus_y[i] = clamp_focus((target-vh/2)*100/(chosen_h[i]-vh));
                        }
                    }
                }
                if (focus_x[i] < 0) focus_x[i] = 0;
                if (focus_x[i] > 100) focus_x[i] = 100;
                if (focus_y[i] < 0) focus_y[i] = 0;
                if (focus_y[i] > 100) focus_y[i] = 100;
                bool moved = old_x != focus_x[i] || old_y != focus_y[i];
                if (moved) { if (fit_mode[i] == 1) fit_mode[i] = 0; redraw = true; }
                if ((keys & (KEY_X | KEY_Y | KEY_L | KEY_R)) || moved) {
                    if (!preview_selected()) strcpy(message, "Apercu impossible");
                    else message[0] = 0;
                    redraw = true;
                }
                if (keys & KEY_A) {
                    clear_preview();
                    phase = phase == 3 ? 4 : (chosen[2][0] && !ends_with(chosen[2], ".bcstm") ? 5 : 6);
                    redraw = true;
                }
            }
        } else if (phase == 5) {
            bool changed = false;
            if (keys & KEY_B) { phase = 4; redraw = true; }
            if (keys & KEY_X) {
                custom_loop = !custom_loop;
                if (!custom_loop) {
                    loop_start_ms = 0;
                    loop_end_ms = audio_info.duration_ms < audio_max_ms() ? audio_info.duration_ms : audio_max_ms();
                    fade_ms = 500;
                }
                changed = true;
            }
            if ((keys & KEY_LEFT) || (keys & KEY_RIGHT)) {
                unsigned previous = audio_quality;
                if ((keys & KEY_LEFT) && audio_quality > 0) --audio_quality;
                if ((keys & KEY_RIGHT) && audio_quality + 1 < AUDIO_QUALITY_COUNT)
                    ++audio_quality;
                if (previous != audio_quality) {
                    if (!custom_loop) {
                        loop_start_ms = 0;
                        loop_end_ms = audio_info.duration_ms < audio_max_ms() ?
                                      audio_info.duration_ms : audio_max_ms();
                    } else if (loop_end_ms > loop_start_ms + audio_max_ms()) {
                        loop_end_ms = loop_start_ms + audio_max_ms();
                    }
                    changed = true;
                }
            }
            if ((keys & KEY_TOUCH) && audio_info.duration_ms) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (touch.px >= 13 && touch.px <= 308 && touch.py >= 58 && touch.py <= 77) {
                    audio_quality = (audio_quality + 1) % AUDIO_QUALITY_COUNT;
                    if (!custom_loop) {
                        loop_start_ms = 0;
                        loop_end_ms = audio_info.duration_ms < audio_max_ms() ?
                                      audio_info.duration_ms : audio_max_ms();
                    } else if (loop_end_ms > loop_start_ms + audio_max_ms()) {
                        loop_end_ms = loop_start_ms + audio_max_ms();
                    }
                    changed = true;
                }
            }
            if ((hidKeysHeld() & KEY_TOUCH) && audio_info.duration_ms) {
                touchPosition touch;
                hidTouchRead(&touch);
                if (touch.px >= 18 && touch.px <= 298 && touch.py >= 153 && touch.py <= 188) {
                    uint32_t at = (uint64_t)(touch.px - 18) * audio_info.duration_ms / 280;
                    at = (at / 100) * 100;
                    custom_loop = true;
                    if (touch.py < 171) {
                        if (at + 1000 >= audio_info.duration_ms)
                            at = audio_info.duration_ms - 1000;
                        if (at != loop_start_ms) {
                            loop_start_ms = at;
                            if (loop_end_ms <= loop_start_ms + 1000 ||
                                loop_end_ms > loop_start_ms + audio_max_ms())
                                loop_end_ms = audio_info.duration_ms < loop_start_ms + audio_max_ms() ?
                                              audio_info.duration_ms : loop_start_ms + audio_max_ms();
                            changed = true;
                        }
                    } else {
                        if (at < 1000) at = 1000;
                        if (at > audio_info.duration_ms) at = audio_info.duration_ms;
                        if (at != loop_end_ms) {
                            loop_end_ms = at;
                            if (loop_end_ms <= loop_start_ms + 1000 ||
                                loop_end_ms > loop_start_ms + audio_max_ms())
                                loop_start_ms = loop_end_ms > audio_max_ms() ? loop_end_ms - audio_max_ms() : 0;
                            changed = true;
                        }
                    }
                }
            }
            if ((keys & KEY_L) && fade_ms >= 100) { fade_ms -= 100; changed = true; }
            if ((keys & KEY_R) && fade_ms < 1000) { fade_ms += 100; changed = true; }
            if (changed) { candidate_ready = false; if (candidate_path[0]) remove(candidate_path); redraw = true; }
            if (keys & KEY_Y) {
                progress_started = 0;
                strcpy(message, "Ecoute du passage..."); draw_ui(); present_frame();
                bool cancelled = false;
                if (!audio_preview_source(chosen[2], loop_start_ms, loop_end_ms,
                                          fade_ms, true, audio_quality,
                                          preview_progress, &cancelled))
                    strcpy(message, cancelled ? "Ecoute arretee" : "Ecoute indisponible");
                else strcpy(message, "Passage ecoute (sans export)");
                redraw = true;
            }
            if (keys & KEY_A) { phase = 6; redraw = true; }
        } else if (phase == 6) {
            if (keys & KEY_B) { phase = chosen[2][0] && !ends_with(chosen[2], ".bcstm") ? 5 : 4; redraw = true; }
            if (keys & KEY_X) { export_zip = !export_zip; redraw = true; }
            if (keys & KEY_Y) { icon_from_bottom = !icon_from_bottom; redraw = true; }
            if (keys & KEY_A) { phase = 7; redraw = true; }
        } else if (phase == 7) {
            if (keys & KEY_B) { phase = 6; redraw = true; }
            if (keys & KEY_Y) {
                SwkbdState keyboard;
                char entered[48] = {0};
                swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 28);
                swkbdSetHintText(&keyboard, "Nom du theme (lettres/chiffres)");
                swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
                if (swkbdInputText(&keyboard, entered, sizeof(entered)) == SWKBD_BUTTON_CONFIRM) {
                    snprintf(custom_theme_name, sizeof(custom_theme_name), "%s", entered);
                    redraw = true;
                }
            }
            if (keys & KEY_A) {
                clear_preview();
                strcpy(message, "Generation en cours..."); draw_ui(); present_frame();
                if (generate_theme()) phase = 8;
                else if (audio_failed_prompt) phase = 9;
                redraw = true;
            }
        } else if (phase == 9) {
            if (keys & KEY_B) { phase = 7; redraw = true; }
            if (keys & KEY_A) {
                chosen[2][0] = 0; candidate_ready = false;
                if (candidate_path[0]) remove(candidate_path);
                strcpy(message, "Creation sans musique..."); draw_ui(); present_frame();
                if (generate_theme()) phase = 8;
                else phase = 7;
                redraw = true;
            }
        } else if (phase == 8 && (keys & KEY_A)) {
            clear_preview(); memset(chosen, 0, sizeof(chosen)); phase = 0;
            custom_theme_name[0] = 0;
            fit_mode[0] = fit_mode[1] = 0;
            zoom_percent[0] = zoom_percent[1] = 100;
            edit_target = 0; focus_x[0] = focus_x[1] = focus_y[0] = focus_y[1] = 50;
            custom_loop = export_zip = icon_from_bottom = candidate_ready = false;
            focus_ready[0] = focus_ready[1] = false;
            if (candidate_path[0]) remove(candidate_path);
            strcpy(cwd, "sdmc:/"); message[0] = 0;
            scan_directory(); show_splash(); redraw = true;
        }
        if ((phase == 3 || phase == 4) && phase != previous_phase) {
            edit_target = phase - 3;
            if (!preview_selected()) strcpy(message, "Apercu impossible");
            redraw = true;
        }
        if (!(hidKeysHeld() & (KEY_UP | KEY_DOWN)) && update_file_dates()) redraw = true;
        if (redraw) { draw_ui(); present_frame(); }
        gspWaitForVBlank();
    }
    clear_preview();
    if (candidate_path[0]) remove(candidate_path);
    ui_shutdown(); romfsExit(); gfxExit();
    return 0;
}
