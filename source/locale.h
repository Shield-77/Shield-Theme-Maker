#ifndef SHIELD_THEME_LOCALE_H
#define SHIELD_THEME_LOCALE_H
#include <stdbool.h>
void locale_set_french(bool french);
bool locale_is_french(void);
const char *tr(const char *french);
#endif
