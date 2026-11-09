#include "audio_convert.h"

#include <3ds.h>
#include <tremor/ivorbisfile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#define OUTPUT_RATE 16000
#define MAX_FRAMES (OUTPUT_RATE * 44)
#define BLOCK_FRAMES 4096
#define SOURCE_FRAMES 4096

typedef struct {
    bool ogg;
    OggVorbis_File vorbis;
    mp3dec_ex_t mp3;
    mp3dec_io_t io;
    FILE *mp3_file;
    int channels, rate;
    int16_t samples[SOURCE_FRAMES * 2];
    size_t buffered, position;
    unsigned read_errors;
} AudioSource;

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void reference(uint8_t *p, uint16_t type, uint32_t offset) {
    put16(p, type); put32(p + 4, offset);
}
static size_t mp3_read(void *data, size_t size, void *context) {
    return fread(data, 1, size, (FILE *)context);
}
static int mp3_seek(uint64_t offset, void *context) {
    return offset > LONG_MAX ? -1 : fseek((FILE *)context, (long)offset, SEEK_SET);
}

static void close_source(AudioSource *source) {
    if (source->ogg) ov_clear(&source->vorbis);
    else {
        mp3dec_ex_close(&source->mp3);
        if (source->mp3_file) fclose(source->mp3_file);
    }
}

static bool open_source(AudioSource *source, const char *path) {
    memset(source, 0, sizeof(*source));
    size_t length = strlen(path);
    source->ogg = length >= 4 && strcasecmp(path + length - 4, ".ogg") == 0;
    if (source->ogg) {
        FILE *file = fopen(path, "rb");
        if (!file) return false;
        if (ov_open(file, &source->vorbis, NULL, 0) != 0) {
            fclose(file); return false;
        }
        vorbis_info *info = ov_info(&source->vorbis, -1);
        if (!info) { close_source(source); return false; }
        source->channels = info->channels;
        source->rate = info->rate;
    } else {
        source->mp3_file = fopen(path, "rb");
        if (!source->mp3_file) return false;
        source->io.read = mp3_read;
        source->io.read_data = source->mp3_file;
        source->io.seek = mp3_seek;
        source->io.seek_data = source->mp3_file;
        if (mp3dec_ex_open_cb(&source->mp3, &source->io, MP3D_DO_NOT_SCAN) != 0) {
            close_source(source); return false;
        }
        source->channels = source->mp3.info.channels;
        source->rate = source->mp3.info.hz;
    }
    if ((source->channels != 1 && source->channels != 2) ||
        source->rate < 8000 || source->rate > 48000) {
        close_source(source); return false;
    }
    return true;
}

static bool next_frame(AudioSource *source, int16_t frame[2]) {
    while (source->position >= source->buffered) {
        source->position = source->buffered = 0;
        if (source->ogg) {
            int stream = 0;
            long count = ov_read(&source->vorbis, (char *)source->samples,
                                 SOURCE_FRAMES * source->channels * sizeof(int16_t), &stream);
            if (count < 0 && ++source->read_errors < 4) continue;
            if (count <= 0) return false;
            source->buffered = count / (source->channels * sizeof(int16_t));
        } else {
            size_t count = mp3dec_ex_read(&source->mp3, source->samples,
                                          SOURCE_FRAMES * source->channels);
            if (!count) return false;
            source->buffered = count / source->channels;
        }
        if (!source->buffered) return false;
    }
    size_t at = source->position++ * source->channels;
    frame[0] = source->samples[at];
    frame[1] = source->channels == 2 ? source->samples[at + 1] : frame[0];
    return true;
}

static bool write_block(FILE *out, const int16_t *left, const int16_t *right, size_t count) {
    size_t bytes = count * 2;
    size_t padding = (32 - bytes % 32) % 32;
    static const uint8_t zero[32] = {0};
    return fwrite(left, 2, count, out) == count &&
           fwrite(zero, 1, padding, out) == padding &&
           fwrite(right, 2, count, out) == count &&
           fwrite(zero, 1, padding, out) == padding;
}

static bool finish_header(FILE *out, uint32_t frames) {
    uint8_t header[0x100] = {0};
    uint32_t blocks = (frames + BLOCK_FRAMES - 1) / BLOCK_FRAMES;
    uint32_t last = frames - (blocks - 1) * BLOCK_FRAMES;
    uint32_t last_bytes = last * 2;
    uint32_t last_padded = (last_bytes + 31) & ~31u;
    long end = ftell(out);
    if (end < 0 || end > 3 * 1024 * 1024 || !frames) return false;
    memcpy(header, "CSTM", 4);
    put16(header + 4, 0xFEFF); put16(header + 6, 0x40);
    put32(header + 8, 0x02000000); put32(header + 0x0c, end);
    put16(header + 0x10, 3);
    reference(header + 0x14, 0x4000, 0x40); put32(header + 0x1c, 0x80);
    reference(header + 0x20, 0x4001, 0xc0); put32(header + 0x28, 0x20);
    reference(header + 0x2c, 0x4002, 0xe0); put32(header + 0x34, end - 0xe0);
    memcpy(header + 0x40, "INFO", 4); put32(header + 0x44, 0x80);
    reference(header + 0x48, 0x4100, 0x18);
    reference(header + 0x50, 0x0101, 0xffffffff);
    reference(header + 0x58, 0x0101, 0x50);
    header[0x60] = 1; header[0x61] = 1; header[0x62] = 2;
    put32(header + 0x64, OUTPUT_RATE);
    put32(header + 0x68, 0); put32(header + 0x6c, frames);
    put32(header + 0x70, blocks);
    put32(header + 0x74, BLOCK_FRAMES * 2);
    put32(header + 0x78, BLOCK_FRAMES);
    put32(header + 0x7c, last_bytes);
    put32(header + 0x80, last);
    put32(header + 0x84, last_padded);
    put32(header + 0x88, 0); put32(header + 0x8c, 0);
    reference(header + 0x90, 0x1f00, 0x18);
    put32(header + 0x98, 2);
    reference(header + 0x9c, 0x4102, 0x14);
    reference(header + 0xa4, 0x4102, 0x1c);
    reference(header + 0xac, 0, 0xffffffff);
    reference(header + 0xb4, 0, 0xffffffff);
    memcpy(header + 0xc0, "SEEK", 4); put32(header + 0xc4, 0x20);
    memcpy(header + 0xe0, "DATA", 4); put32(header + 0xe4, end - 0xe0);
    if (fseek(out, 0, SEEK_SET) != 0 || fwrite(header, 1, sizeof(header), out) != sizeof(header))
        return false;
    return fflush(out) == 0;
}

typedef struct { const char *source, *destination; bool result; } Job;

static void convert_worker(void *arg) {
    Job *job = arg;
    AudioSource *input = calloc(1, sizeof(*input));
    if (!input) return;
    if (!open_source(input, job->source)) { free(input); return; }
    FILE *out = fopen(job->destination, "wb");
    if (!out) { close_source(input); free(input); return; }
    uint8_t empty[0x100] = {0};
    bool ok = fwrite(empty, 1, sizeof(empty), out) == sizeof(empty);
    int16_t *left = malloc(BLOCK_FRAMES * sizeof(int16_t));
    int16_t *right = malloc(BLOCK_FRAMES * sizeof(int16_t));
    if (!left || !right) ok = false;
    int16_t current[2], next[2];
    if (ok) ok = next_frame(input, current) && next_frame(input, next);
    uint32_t phase = 0, step = ((uint64_t)input->rate << 16) / OUTPUT_RATE;
    uint32_t frames = 0; size_t in_block = 0;
    bool more = true;
    while (ok && more && frames < MAX_FRAMES) {
        for (int channel = 0; channel < 2; ++channel) {
            int32_t value = ((int32_t)current[channel] * (int32_t)(65536 - phase) +
                             (int32_t)next[channel] * (int32_t)phase) >> 16;
            (channel == 0 ? left : right)[in_block] = (int16_t)value;
        }
        ++frames; ++in_block;
        if (in_block == BLOCK_FRAMES) {
            ok = write_block(out, left, right, in_block);
            in_block = 0;
        }
        phase += step;
        while (phase >= 65536 && more) {
            phase -= 65536;
            current[0] = next[0]; current[1] = next[1];
            more = next_frame(input, next);
        }
    }
    if (ok && in_block) ok = write_block(out, left, right, in_block);
    if (ok) ok = finish_header(out, frames);
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(job->destination);
    job->result = ok;
    free(left); free(right); close_source(input); free(input);
}

bool convert_audio_to_bcstm(const char *source, const char *destination) {
    Job job = {source, destination, false};
    Thread worker = threadCreate(convert_worker, &job, 0x20000, 0x30, -2, false);
    if (!worker) return false;
    threadJoin(worker, U64_MAX);
    threadFree(worker);
    return job.result;
}
