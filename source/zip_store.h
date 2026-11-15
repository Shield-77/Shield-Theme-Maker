#ifndef SHIELD_ZIP_STORE_H
#define SHIELD_ZIP_STORE_H
#include <stdbool.h>
#include <stddef.h>

/* Root-level, stored ZIP entries. Source data is already compressed. */
bool zip_store_files(const char *destination, const char *directory,
                     const char *const *names, size_t count);
bool zip_verify(const char *path, const char *const *names, size_t count);
#endif
