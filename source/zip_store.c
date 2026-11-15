#include "zip_store.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZIP_MAX 6
typedef struct { uint32_t crc, size, at; const char *name; } Item;
static uint32_t table[256];
static void init_crc(void) {
    if (table[1]) return;
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (unsigned b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
        table[i] = c;
    }
}
static uint32_t crc_next(uint32_t crc, const uint8_t *data, size_t size) {
    while (size--) crc = table[(crc ^ *data++) & 255] ^ (crc >> 8);
    return crc;
}
static bool w16(FILE *f, uint16_t v) {
    uint8_t p[2] = {(uint8_t)v, (uint8_t)(v>>8)};
    return fwrite(p, 1, 2, f) == 2;
}
static bool w32(FILE *f, uint32_t v) { return w16(f, (uint16_t)v) && w16(f, (uint16_t)(v>>16)); }
static uint16_t r16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t r32(const uint8_t *p) { return r16(p) | ((uint32_t)r16(p+2)<<16); }

bool zip_store_files(const char *dest, const char *dir,
                     const char *const *names, size_t count) {
    if (!dest || !dir || !names || !count || count > ZIP_MAX) return false;
    init_crc();
    FILE *out = fopen(dest, "wb");
    if (!out) return false;
    Item items[ZIP_MAX] = {{0}};
    bool ok = true;
    uint8_t buffer[8192];
    for (size_t i = 0; i < count && ok; ++i) {
        char path[512];
        size_t name_len = strlen(names[i]);
        if (!name_len || name_len > 100 || strchr(names[i], '/') || strchr(names[i], '\\') ||
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]) >= (int)sizeof(path)) { ok = false; break; }
        FILE *in = fopen(path, "rb");
        if (!in) { ok = false; break; }
        long at = ftell(out);
        items[i] = (Item){.at = (uint32_t)at, .name = names[i]};
        ok = at >= 0 && w32(out, 0x04034b50) && w16(out, 20) && w16(out, 0) &&
             w16(out, 0) && w16(out, 0) && w16(out, 0) &&
             w32(out, 0) && w32(out, 0) && w32(out, 0) &&
             w16(out, (uint16_t)name_len) && w16(out, 0) &&
             fwrite(names[i], 1, name_len, out) == name_len;
        uint32_t crc = 0xffffffffu, length = 0;
        size_t n;
        while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
            if (length > UINT32_MAX - n || fwrite(buffer, 1, n, out) != n) { ok = false; break; }
            length += (uint32_t)n;
            crc = crc_next(crc, buffer, n);
        }
        if (ferror(in)) ok = false;
        if (fclose(in) != 0) ok = false;
        long after = ftell(out);
        items[i].crc = crc ^ 0xffffffffu; items[i].size = length;
        if (ok) ok = after >= 0 && fseek(out, at + 14, SEEK_SET) == 0 &&
                     w32(out, items[i].crc) && w32(out, length) && w32(out, length) &&
                     fseek(out, after, SEEK_SET) == 0;
    }
    long central = ftell(out);
    for (size_t i = 0; i < count && ok; ++i) {
        size_t n = strlen(items[i].name);
        ok = w32(out, 0x02014b50) && w16(out, 20) && w16(out, 20) &&
             w16(out, 0) && w16(out, 0) && w16(out, 0) && w16(out, 0) &&
             w32(out, items[i].crc) && w32(out, items[i].size) && w32(out, items[i].size) &&
             w16(out, (uint16_t)n) && w16(out, 0) && w16(out, 0) &&
             w16(out, 0) && w16(out, 0) && w32(out, 0) && w32(out, items[i].at) &&
             fwrite(items[i].name, 1, n, out) == n;
    }
    long end = ftell(out);
    if (ok) ok = central >= 0 && end >= central && w32(out, 0x06054b50) &&
                 w16(out, 0) && w16(out, 0) && w16(out, count) && w16(out, count) &&
                 w32(out, (uint32_t)(end-central)) && w32(out, (uint32_t)central) && w16(out, 0);
    if (fflush(out) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(dest);
    return ok;
}

bool zip_verify(const char *path, const char *const *names, size_t count) {
    if (!path || !names || !count || count > ZIP_MAX) return false;
    init_crc();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = true;
    uint8_t h[30], buffer[8192];
    Item items[ZIP_MAX] = {{0}};
    for (size_t i = 0; i < count && ok; ++i) {
        long at = ftell(f);
        ok = fread(h, 1, sizeof(h), f) == sizeof(h) && r32(h) == 0x04034b50 &&
             r16(h+8) == 0 && r16(h+26) == strlen(names[i]) && r16(h+28) == 0;
        if (!ok) break;
        char name[101] = {0};
        uint32_t length = r32(h+18), remaining = length, crc = 0xffffffffu;
        if (r16(h+26) > 100 || fread(name, 1, r16(h+26), f) != r16(h+26) ||
            strcmp(name, names[i]) || length != r32(h+22)) { ok = false; break; }
        while (remaining && ok) {
            size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (fread(buffer, 1, chunk, f) != chunk) { ok = false; break; }
            crc = crc_next(crc, buffer, chunk); remaining -= chunk;
        }
        if ((crc ^ 0xffffffffu) != r32(h+14)) ok = false;
        items[i] = (Item){.crc = r32(h+14), .size = length, .at = (uint32_t)at,
                          .name = names[i]};
    }
    if (ok) {
        long central = ftell(f);
        for (size_t i = 0; i < count && ok; ++i) {
            uint8_t c[46];
            ok = fread(c, 1, sizeof(c), f) == sizeof(c) && r32(c) == 0x02014b50 &&
                 r32(c+16) == items[i].crc && r32(c+20) == items[i].size &&
                 r32(c+24) == items[i].size && r32(c+42) == items[i].at &&
                 r16(c+28) == strlen(names[i]) && !r16(c+30) && !r16(c+32);
            if (ok) {
                char name[101] = {0};
                ok = r16(c+28) <= 100 && fread(name, 1, r16(c+28), f) == r16(c+28) &&
                     strcmp(name, names[i]) == 0;
            }
        }
        uint8_t e[22];
        if (ok) ok = fread(e, 1, sizeof(e), f) == sizeof(e) && r32(e) == 0x06054b50 &&
                     r16(e+10) == count && r32(e+16) == (uint32_t)central &&
                     r32(e+12) == (uint32_t)(ftell(f) - 22 - central);
    }
    fclose(f);
    return ok;
}
