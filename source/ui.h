#ifndef SHIELD_THEME_UI_H
#define SHIELD_THEME_UI_H
#include <stdbool.h>
#include <stdint.h>

bool ui_init(void);
void ui_shutdown(void);
void ui_begin(const char *title, const char *step);
void ui_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
void ui_text(int x, int y, const char *text, uint8_t r, uint8_t g, uint8_t b);
void ui_footer(const char *text);
void ui_top_status(const char *label, int percent, unsigned elapsed_s, unsigned tick);

#endif
