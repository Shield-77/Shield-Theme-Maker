#include "ui.h"
#include "locale.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *atlas, *frame;
static void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!frame || x < 0 || y < 0 || x >= 320 || y >= 240) return;
    size_t at = ((size_t)x * 240 + (239 - y)) * 3;
    frame[at] = b; frame[at+1] = g; frame[at+2] = r;
}
bool ui_init(void) {
    FILE *f = fopen("romfs:/font.bin", "rb");
    if (!f) return false;
    atlas = malloc(95 * 160);
    bool ok = atlas && fread(atlas, 1, 95 * 160, f) == 95 * 160;
    fclose(f);
    if (!ok) { free(atlas); atlas = NULL; }
    return ok;
}
void ui_shutdown(void) { free(atlas); atlas = NULL; }
void ui_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for (int yy = y; yy < y+h; ++yy)
        for (int xx = x; xx < x+w; ++xx) pixel(xx, yy, r, g, b);
}
void ui_text(int x, int y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    if (!atlas || !frame || !text) return;
    text = tr(text);
    for (int letter = 0; text[letter] && x + letter * 10 < 320; ++letter) {
        unsigned char c = (unsigned char)text[letter];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = atlas + (c - 32) * 160;
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 10; ++col) {
                int px = x + letter * 10 + col, py = y + row;
                if (px >= 320 || py >= 240) continue;
                uint8_t a = glyph[row*10+col];
                if (!a) continue;
                size_t at = ((size_t)px * 240 + (239-py)) * 3;
                frame[at] = (frame[at]*(255-a) + b*a)/255;
                frame[at+1] = (frame[at+1]*(255-a) + g*a)/255;
                frame[at+2] = (frame[at+2]*(255-a) + r*a)/255;
            }
    }
}
void ui_begin(const char *title, const char *step) {
    frame = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!frame) return;
    ui_box(0, 0, 320, 240, 6, 11, 39);
    ui_box(0, 0, 320, 32, 8, 38, 69);
    ui_box(0, 30, 320, 2, 24, 211, 244);
    ui_text(8, 7, title, 244, 250, 255);
    ui_text(8, 36, step, 101, 215, 241);
}
void ui_footer(const char *text) {
    ui_box(0, 211, 320, 29, 8, 38, 69);
    ui_box(0, 211, 320, 1, 24, 211, 244);
    ui_text(6, 217, text, 240, 247, 250);
}

static void top_box(uint8_t *top, int x, int y, int w, int h,
                    uint8_t r, uint8_t g, uint8_t b) {
    for (int xx = x; xx < x+w; ++xx) for (int yy = y; yy < y+h; ++yy) {
        size_t at = ((size_t)xx * 240 + (239-yy)) * 3;
        top[at] = b; top[at+1] = g; top[at+2] = r;
    }
}

void ui_top_status(const char *label, int percent, unsigned elapsed_s, unsigned tick) {
    uint8_t *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (!top || !atlas) return;
    top_box(top, 0, 203, 400, 37, 6, 11, 39);
    const char *shown = tr(label);
    char caption[48];
    snprintf(caption, sizeof(caption), "%.29s  %us %c", shown,
             elapsed_s, "|/-\\"[tick & 3]);
    for (int i = 0; caption[i] && i < 39; ++i) {
        unsigned char c = (unsigned char)caption[i];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = atlas + (c - 32) * 160;
        for (int row = 0; row < 16; ++row) for (int col = 0; col < 10; ++col) {
            int px = 6 + i*10 + col, py = 207 + row;
            if (px >= 400) continue;
            uint8_t a = glyph[row*10+col];
            if (!a) continue;
            size_t at = ((size_t)px*240 + (239-py))*3;
            top[at] = (top[at]*(255-a) + 244*a)/255;
            top[at+1] = (top[at+1]*(255-a) + 250*a)/255;
            top[at+2] = (top[at+2]*(255-a) + 255*a)/255;
        }
    }
    top_box(top, 6, 229, 388, 7, 18, 57, 78);
    if (percent >= 0) {
        if (percent > 100) percent = 100;
        top_box(top, 6, 229, percent * 388 / 100, 7, 24, 211, 244);
    } else {
        int left = (tick * 13) % 338;
        top_box(top, 6 + left, 229, 50, 7, 24, 211, 244);
    }
}
