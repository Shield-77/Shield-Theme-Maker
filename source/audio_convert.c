#include "audio_convert.h"
#include "bcstm_validate.h"

#include <3ds.h>
#include <tremor/ivorbisfile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#define MAX_FRAMES 840000u
#define BLOCK_FRAMES 4096
#define SOURCE_FRAMES 4096
#define INFO_SIZE 0xa0u
#define SEEK_AT 0xe0u
#define MAX_SEEK_SIZE (((8u + ((MAX_FRAMES + BLOCK_FRAMES - 1) / BLOCK_FRAMES) * 8u) + 31u) & ~31u)
#define RESERVED_SAMPLES_AT (SEEK_AT + MAX_SEEK_SIZE + 0x20u)

AudioProfile audio_profile(unsigned quality) {
    static const AudioProfile profiles[AUDIO_QUALITY_COUNT] = {
        {8000, 105000}, {10000, 84000}, {12000, 70000},
        {14000, 60000}, {16000, 52000},
        {18000, 46000}, {20000, 41000}
    };
    return profiles[quality < AUDIO_QUALITY_COUNT ? quality : 4];
}

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
        if (mp3dec_ex_open_cb(&source->mp3, &source->io,
                              MP3D_SEEK_TO_SAMPLE | MP3D_DO_NOT_SCAN) != 0) {
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

bool audio_probe(const char *path, AudioInfo *info) {
    if (!info) return false;
    *info = (AudioInfo){0};
    AudioSource *source = calloc(1, sizeof(*source));
    if (!source) return false;
    bool ok = open_source(source, path);
    if (ok) {
        if (source->ogg) {
            ogg_int64_t frames = ov_pcm_total(&source->vorbis, -1);
            if (frames > 0) info->duration_ms = (uint32_t)((uint64_t)frames * 1000 / source->rate);
        } else if (source->mp3.samples) {
            info->duration_ms = (uint32_t)(source->mp3.samples * 1000ULL /
                                                  (source->channels * (uint64_t)source->rate));
        } else {
            long bytes = 0;
            if (!fseek(source->mp3_file, 0, SEEK_END)) bytes = ftell(source->mp3_file);
            if (bytes > 0 && source->mp3.info.bitrate_kbps > 0) {
                info->duration_ms = (uint32_t)((uint64_t)bytes * 8 /
                                                source->mp3.info.bitrate_kbps);
                info->approximate = true;
            }
        }
        ok = info->duration_ms >= 1000;
        close_source(source);
    }
    free(source);
    return ok;
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

static bool seek_source(AudioSource *source, uint32_t at_ms) {
    uint64_t frame = (uint64_t)at_ms * source->rate / 1000;
    int result = source->ogg ?
        ov_pcm_seek(&source->vorbis, (ogg_int64_t)frame) :
        mp3dec_ex_seek(&source->mp3, frame * source->channels);
    source->position = source->buffered = 0;
    return result == 0;
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

static bool finish_header(FILE *out, uint32_t frames, uint32_t loop_start,
                          uint32_t output_rate) {
    uint8_t header[SEEK_AT] = {0};
    if (!frames) return false;
    uint32_t blocks = (frames + BLOCK_FRAMES - 1) / BLOCK_FRAMES;
    uint32_t last = frames - (blocks - 1) * BLOCK_FRAMES;
    uint32_t last_bytes = last * 2;
    uint32_t last_padded = (last_bytes + 31) & ~31u;
    uint32_t seek_size = (8u + blocks * 8u + 31u) & ~31u;
    uint32_t data_at = SEEK_AT + seek_size;
    uint32_t samples_at = data_at + 0x20u;
    uint32_t sample_bytes = 2u * ((blocks - 1u) * BLOCK_FRAMES * 2u + last_padded);
    uint32_t end = samples_at + sample_bytes;
    if (!frames || blocks > (MAX_FRAMES + BLOCK_FRAMES - 1) / BLOCK_FRAMES ||
        end > 0x337000 || samples_at > RESERVED_SAMPLES_AT ||
        fflush(out) != 0) return false;
    /* Move the samples forward through the file, preserving overlapping data. */
    uint8_t copy[16384];
    for (uint32_t at = 0; at < sample_bytes; at += sizeof(copy)) {
        size_t count = sample_bytes - at < sizeof(copy) ? sample_bytes - at : sizeof(copy);
        if (fseek(out, RESERVED_SAMPLES_AT + at, SEEK_SET) ||
            fread(copy, 1, count, out) != count ||
            fseek(out, samples_at + at, SEEK_SET) ||
            fwrite(copy, 1, count, out) != count) return false;
    }
    if (fflush(out) != 0 || ftruncate(fileno(out), end) != 0) return false;
    memcpy(header, "CSTM", 4);
    put16(header + 4, 0xFEFF); put16(header + 6, 0x40);
    put32(header + 8, 0x02000000); put32(header + 0x0c, end);
    put16(header + 0x10, 3);
    reference(header + 0x14, 0x4000, 0x40); put32(header + 0x1c, INFO_SIZE);
    reference(header + 0x20, 0x4001, SEEK_AT); put32(header + 0x28, seek_size);
    reference(header + 0x2c, 0x4002, data_at); put32(header + 0x34, end - data_at);
    memcpy(header + 0x40, "INFO", 4); put32(header + 0x44, INFO_SIZE);
    reference(header + 0x48, 0x4100, 0x18);
    reference(header + 0x50, 0x0101, 0x50);
    reference(header + 0x58, 0x0101, 0x5c);
    header[0x60] = 1; header[0x61] = 1; header[0x62] = 2;
    put32(header + 0x64, output_rate);
    put32(header + 0x68, loop_start); put32(header + 0x6c, frames);
    put32(header + 0x70, blocks);
    put32(header + 0x74, BLOCK_FRAMES * 2);
    put32(header + 0x78, BLOCK_FRAMES);
    put32(header + 0x7c, last_bytes);
    put32(header + 0x80, last);
    put32(header + 0x84, last_padded);
    put32(header + 0x88, 4); put32(header + 0x8c, BLOCK_FRAMES);
    reference(header + 0x90, 0x1f00, 0x18);
    put32(header + 0x98, 1);
    reference(header + 0x9c, 0x4101, 0x20);
    put32(header + 0xa4, 2);
    reference(header + 0xa8, 0x4102, 0x28);
    reference(header + 0xb0, 0x4102, 0x30);
    header[0xb8] = 0x7f; header[0xb9] = 0x40;
    reference(header + 0xbc, 0x0100, 0x0c);
    put32(header + 0xc4, 2);
    header[0xc8] = 0; header[0xc9] = 1;
    reference(header + 0xcc, 0, 0xffffffff);
    reference(header + 0xd4, 0, 0xffffffff);
    uint8_t block_header[8] = {0};
    if (fseek(out, 0, SEEK_SET) || fwrite(header, 1, sizeof(header), out) != sizeof(header)) return false;
    memcpy(block_header, "SEEK", 4); put32(block_header + 4, seek_size);
    if (fseek(out, SEEK_AT, SEEK_SET) || fwrite(block_header, 1, 8, out) != 8) return false;
    memcpy(block_header, "DATA", 4); put32(block_header + 4, end - data_at);
    if (fseek(out, data_at, SEEK_SET) || fwrite(block_header, 1, 8, out) != 8) return false;
    return fflush(out) == 0;
}

static bool patch_crossfade(const char *path, const int16_t *head,
                            uint32_t frames, uint32_t fade) {
    if (!fade) return true;
    FILE *f = fopen(path, "r+b");
    int16_t *left = malloc(BLOCK_FRAMES * sizeof(int16_t));
    int16_t *right = malloc(BLOCK_FRAMES * sizeof(int16_t));
    if (!f || !left || !right) {
        if (f) fclose(f);
        free(left); free(right);
        return false;
    }
    bool ok = true;
    uint32_t blocks = (frames + BLOCK_FRAMES - 1) / BLOCK_FRAMES;
    uint32_t sample_at = SEEK_AT + ((8u + blocks * 8u + 31u) & ~31u) + 0x20u;
    uint32_t first = frames - fade;
    uint32_t last_block = (frames - 1) / BLOCK_FRAMES;
    for (uint32_t block = first / BLOCK_FRAMES; block <= last_block && ok; ++block) {
        uint32_t begin = block * BLOCK_FRAMES;
        uint32_t count = block == last_block ? frames - begin : BLOCK_FRAMES;
        uint32_t padded = (count * 2 + 31) & ~31u;
        long pos = sample_at + (long)block * BLOCK_FRAMES * 4;
        ok = fseek(f, pos, SEEK_SET) == 0 && fread(left, 2, count, f) == count &&
             fseek(f, pos + padded, SEEK_SET) == 0 && fread(right, 2, count, f) == count;
        for (uint32_t j = first > begin ? first - begin : 0; j < count && ok; ++j) {
            uint32_t i = begin + j - first;
            int32_t tail_weight = (int32_t)(fade - i - 1);
            int32_t head_weight = (int32_t)(i + 1);
            left[j] = (int16_t)((left[j] * tail_weight + head[2*i] * head_weight) / (int32_t)fade);
            right[j] = (int16_t)((right[j] * tail_weight + head[2*i+1] * head_weight) / (int32_t)fade);
        }
        if (ok) ok = fseek(f, pos, SEEK_SET) == 0 && fwrite(left, 2, count, f) == count &&
                     fseek(f, pos + padded, SEEK_SET) == 0 && fwrite(right, 2, count, f) == count;
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    free(left); free(right);
    return ok;
}

typedef struct {
    const char *source, *destination;
    uint32_t start_ms, end_ms, fade_ms;
    bool automatic;
    unsigned quality;
    volatile uint32_t frames_done;
    volatile bool done, cancel;
    bool result;
} Job;

static void convert_worker(void *arg) {
    Job *job = arg;
    AudioProfile profile = audio_profile(job->quality);
    uint32_t output_rate = profile.rate;
    AudioSource *input = calloc(1, sizeof(*input));
    if (!input) { job->done = true; return; }
    if (!open_source(input, job->source)) { free(input); job->done = true; return; }
    FILE *out = fopen(job->destination, "w+b");
    if (!out) { close_source(input); free(input); job->done = true; return; }
    uint8_t empty[RESERVED_SAMPLES_AT] = {0};
    bool ok = fwrite(empty, 1, sizeof(empty), out) == sizeof(empty);
    int16_t *left = malloc(BLOCK_FRAMES * sizeof(int16_t));
    int16_t *right = malloc(BLOCK_FRAMES * sizeof(int16_t));
    uint32_t requested = (uint64_t)(job->end_ms - job->start_ms) * output_rate / 1000;
    uint32_t profile_max = (uint64_t)profile.max_ms * output_rate / 1000;
    if (requested > profile_max) requested = profile_max;
    uint32_t fade = (uint64_t)job->fade_ms * output_rate / 1000;
    if (fade > output_rate) fade = output_rate;
    if (fade > requested / 3) fade = requested / 3;
    int16_t *head = fade ? malloc((size_t)fade * 2 * sizeof(int16_t)) : NULL;
    int16_t *ring = job->automatic && fade ? malloc((size_t)fade * 2 * sizeof(int16_t)) : NULL;
    if (!left || !right || (fade && !head) || (job->automatic && fade && !ring) ||
        requested < output_rate) ok = false;
    uint64_t skip = (uint64_t)job->start_ms * input->rate / 1000;
    int16_t discard[2];
    if (ok && input->ogg && ov_pcm_seek(&input->vorbis, (ogg_int64_t)skip) == 0) skip = 0;
    while (ok && skip--) if (!next_frame(input, discard) || job->cancel) ok = false;
    int16_t current[2], next[2];
    if (ok) ok = next_frame(input, current) && next_frame(input, next);
    uint32_t phase = 0, step = ((uint64_t)input->rate << 16) / output_rate;
    uint32_t frames = 0; size_t in_block = 0;
    uint32_t best_frames = 0;
    uint64_t best_score = UINT64_MAX;
    bool more = true;
    while (ok && more && frames < requested && !job->cancel) {
        for (int channel = 0; channel < 2; ++channel) {
            int32_t value = ((int32_t)current[channel] * (int32_t)(65536 - phase) +
                             (int32_t)next[channel] * (int32_t)phase) >> 16;
            (channel == 0 ? left : right)[in_block] = (int16_t)value;
        }
        if (frames < fade) {
            head[2*frames] = left[in_block];
            head[2*frames+1] = right[in_block];
        }
        if (ring) {
            ring[2*(frames%fade)] = left[in_block];
            ring[2*(frames%fade)+1] = right[in_block];
        }
        ++frames; ++in_block;
        if (in_block == BLOCK_FRAMES) {
            ok = write_block(out, left, right, in_block);
            in_block = 0;
            job->frames_done = frames;
            /* Keep the chosen duration: automatic seam search may trim at most
               the final two seconds, never half of the requested passage. */
            if (ok && ring && frames + output_rate*2 >= requested &&
                frames >= output_rate*8 &&
                frames >= fade*3) {
                uint64_t score = 0;
                uint32_t step_score = fade/128;
                if (!step_score) step_score = 1;
                for (uint32_t j = 0; j < fade; j += step_score) {
                    uint32_t at = ((frames-fade+j)%fade)*2;
                    score += abs((int)ring[at] - (int)head[2*j]);
                    score += abs((int)ring[at+1] - (int)head[2*j+1]);
                }
                if (score < best_score) { best_score = score; best_frames = frames; }
            }
        }
        phase += step;
        while (phase >= 65536 && more) {
            phase -= 65536;
            current[0] = next[0]; current[1] = next[1];
            more = next_frame(input, next);
        }
    }
    if (frames < output_rate || job->cancel) ok = false;
    if (fade > frames / 3) fade = frames / 3;
    if (ok && in_block) ok = write_block(out, left, right, in_block);
    if (ok && best_frames && best_frames < frames) {
        long chosen_end = RESERVED_SAMPLES_AT + (long)best_frames * 4;
        if (fflush(out) != 0) ok = false;
        else if (ftruncate(fileno(out), chosen_end) == 0) {
            if (fseek(out, chosen_end, SEEK_SET) == 0) frames = best_frames;
            else ok = false;
        } else if (fseek(out, 0, SEEK_END) != 0) ok = false;
    }
    /* The tail already contains the first fade samples. Resume after those
       samples so the loop does not play the beginning twice. */
    if (ok) ok = finish_header(out, frames, fade, output_rate);
    if (fclose(out) != 0) ok = false;
    if (ok) ok = patch_crossfade(job->destination, head, frames, fade);
    BcstmInfo checked;
    if (ok) ok = bcstm_validate(job->destination, &checked) &&
                 checked.encoding == 1 && checked.channels == 2 &&
                 checked.sample_rate == output_rate &&
                 checked.loop_start == fade && checked.loop_end == frames;
    if (!ok) remove(job->destination);
    job->result = ok;
    free(left); free(right); free(head); free(ring); close_source(input); free(input);
    job->done = true;
}

bool convert_audio_to_bcstm(const char *source, const char *destination,
                            uint32_t start_ms, uint32_t end_ms, uint32_t fade_ms,
                            bool automatic, unsigned quality,
                            void (*progress)(unsigned percent)) {
    if (!source || !destination || end_ms <= start_ms || end_ms - start_ms < 1000) return false;
    Job job = {.source = source, .destination = destination,
               .start_ms = start_ms, .end_ms = end_ms, .fade_ms = fade_ms,
               .automatic = automatic, .quality = quality};
    aptSetSleepAllowed(false);
    Thread worker = threadCreate(convert_worker, &job, 0x10000, 0x30, -2, false);
    if (!worker) { aptSetSleepAllowed(true); return false; }
    unsigned last = 101;
    uint64_t last_update = 0;
    while (!job.done) {
        AudioProfile profile = audio_profile(quality);
        uint32_t target = (uint64_t)(end_ms - start_ms) * profile.rate / 1000;
        uint32_t maximum = (uint64_t)profile.max_ms * profile.rate / 1000;
        if (target > maximum) target = maximum;
        unsigned percent = target ? job.frames_done * 100u / target : 0;
        uint64_t now = osGetTime();
        if (progress && (percent != last || now - last_update >= 250)) {
            progress(percent); last = percent; last_update = now;
        }
        if (!aptMainLoop()) job.cancel = true;
        hidScanInput();
        if (hidKeysDown() & KEY_B) job.cancel = true;
        gspWaitForVBlank();
    }
    threadJoin(worker, U64_MAX);
    threadFree(worker);
    aptSetSleepAllowed(true);
    return job.result;
}

#define PREVIEW_BUFFERS 3
#define PREVIEW_BUFFER_FRAMES 4096

typedef struct {
    const char *source;
    uint32_t start_ms, end_ms, fade_ms;
    bool repeat;
    unsigned quality;
    int16_t *pcm[PREVIEW_BUFFERS];
    ndspWaveBuf wave[PREVIEW_BUFFERS];
    volatile uint32_t frames_done;
    volatile uint32_t frames_target;
    volatile bool done, cancel;
    bool result;
} PreviewJob;

static void source_preview_worker(void *arg) {
    PreviewJob *job = arg;
    AudioProfile profile = audio_profile(job->quality);
    uint32_t output_rate = profile.rate;
    AudioSource *input = calloc(1, sizeof(*input));
    bool opened = input && open_source(input, job->source);
    bool ok = opened && (!job->start_ms || seek_source(input, job->start_ms));
    int16_t current[2], next[2];
    if (ok) ok = next_frame(input, current) && next_frame(input, next);
    uint32_t requested = (uint64_t)(job->end_ms - job->start_ms) * output_rate / 1000;
    uint32_t maximum = (uint64_t)profile.max_ms * output_rate / 1000;
    if (requested > maximum) requested = maximum;
    uint32_t pass_target = requested;
    job->frames_target = pass_target;
    uint32_t step = ok ? ((uint64_t)input->rate << 16) / output_rate : 0;
    uint32_t fade = (uint64_t)job->fade_ms * output_rate / 1000;
    if (fade > output_rate) fade = output_rate;
    if (fade > requested / 3) fade = requested / 3;
    int16_t *head = fade ? malloc((size_t)fade * 4) : NULL;
    int16_t *tail = fade ? malloc((size_t)fade * 4) : NULL;
    if (fade && (!head || !tail)) ok = false;
    uint32_t phase = 0, written = 0, held = 0, tail_at = 0, seam_at = 0;
    unsigned slot = 0;
    bool more = ok;
    bool seam = false, finished = false, first_pass = true;
    while (ok && !job->cancel && !finished) {
        ndspWaveBuf *buf = &job->wave[slot];
        if (buf->status != NDSP_WBUF_DONE) {
            svcSleepThread(2 * 1000 * 1000);
            continue;
        }
        unsigned count = 0;
        while (count < PREVIEW_BUFFER_FRAMES && ok && !job->cancel && !finished) {
            if (seam) {
                if (seam_at < held) {
                    uint32_t tail_frame = ((tail_at + seam_at) % fade) * 2;
                    for (unsigned c = 0; c < 2; ++c) {
                        int32_t tail_weight = (int32_t)(held - seam_at - 1);
                        int32_t head_weight = (int32_t)(seam_at + 1);
                        job->pcm[slot][2*count+c] = (int16_t)(
                            (tail[tail_frame+c] * tail_weight +
                             head[2*seam_at+c] * head_weight) / (int32_t)held);
                    }
                    ++count;
                    ++seam_at;
                    continue;
                }
                if (!job->repeat) {
                    finished = true;
                    break;
                }
                if (first_pass && written < output_rate) {
                    ok = false;
                    break;
                }
                /* The first fade samples are already in the seam. Resume just
                   after them. Reopening at zero avoids indexing a long MP3. */
                if (!input->ogg && !job->start_ms) {
                    close_source(input);
                    opened = false;
                    ok = open_source(input, job->source);
                    opened = ok;
                    uint64_t skip = ok ? (uint64_t)fade * input->rate / output_rate : 0;
                    int16_t discard[2];
                    while (ok && skip && !job->cancel) {
                        ok = next_frame(input, discard);
                        --skip;
                    }
                } else {
                    uint32_t resume_ms = job->start_ms +
                                         (uint64_t)fade * 1000 / output_rate;
                    ok = seek_source(input, resume_ms);
                }
                if (!ok) break;
                ok = next_frame(input, current) && next_frame(input, next);
                phase = written = held = tail_at = seam_at = 0;
                pass_target = requested - fade;
                job->frames_target = pass_target;
                first_pass = false;
                more = ok;
                seam = false;
                continue;
            }
            if (written >= pass_target || !more) {
                seam = true;
                seam_at = 0;
                continue;
            }
            int16_t frame[2];
            for (unsigned c = 0; c < 2; ++c)
                frame[c] = (int16_t)(
                    ((int32_t)current[c] * (int32_t)(65536 - phase) +
                     (int32_t)next[c] * (int32_t)phase) >> 16);
            if (fade) {
                if (first_pass && written < fade) {
                    head[2*written] = frame[0];
                    head[2*written+1] = frame[1];
                }
                if (held < fade) {
                    tail[2*held] = frame[0];
                    tail[2*held+1] = frame[1];
                    ++held;
                } else {
                    job->pcm[slot][2*count] = tail[2*tail_at];
                    job->pcm[slot][2*count+1] = tail[2*tail_at+1];
                    tail[2*tail_at] = frame[0];
                    tail[2*tail_at+1] = frame[1];
                    tail_at = (tail_at + 1) % fade;
                    ++count;
                }
            } else {
                job->pcm[slot][2*count] = frame[0];
                job->pcm[slot][2*count+1] = frame[1];
                ++count;
            }
            ++written;
            phase += step;
            while (phase >= 65536 && more) {
                phase -= 65536;
                current[0] = next[0]; current[1] = next[1];
                more = next_frame(input, next);
            }
        }
        if (count) {
            buf->data_vaddr = job->pcm[slot];
            buf->nsamples = count;
            DSP_FlushDataCache(job->pcm[slot], count * 4);
            ndspChnWaveBufAdd(0, buf);
            job->frames_done = written;
            slot = (slot + 1) % PREVIEW_BUFFERS;
        }
    }
    while (ok && finished && !job->cancel) {
        bool playing = false;
        for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i)
            if (job->wave[i].status != NDSP_WBUF_DONE) playing = true;
        if (!playing) break;
        svcSleepThread(2 * 1000 * 1000);
    }
    job->result = ok && finished && !job->cancel;
    free(head);
    free(tail);
    if (opened) close_source(input);
    free(input);
    job->done = true;
}

bool audio_preview_source(const char *source, uint32_t start_ms, uint32_t end_ms,
                          uint32_t fade_ms, bool repeat, unsigned quality,
                          void (*progress)(unsigned percent),
                          bool *cancelled) {
    if (cancelled) *cancelled = false;
    if (!source || end_ms <= start_ms || end_ms - start_ms < 1000) return false;
    AudioProfile profile = audio_profile(quality);
    aptSetSleepAllowed(false);
    if (ndspInit() != 0) { aptSetSleepAllowed(true); return false; }
    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, profile.rate);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    float mix[12] = {0}; mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    PreviewJob job = {.source = source, .start_ms = start_ms, .end_ms = end_ms,
                      .fade_ms = fade_ms, .repeat = repeat, .quality = quality};
    bool allocated = true;
    for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i) {
        job.pcm[i] = linearAlloc(PREVIEW_BUFFER_FRAMES * 4);
        if (!job.pcm[i]) allocated = false;
        job.wave[i].status = NDSP_WBUF_DONE;
    }
    Thread worker = NULL;
    if (allocated) worker = threadCreate(source_preview_worker, &job, 0x10000, 0x30, -2, false);
    if (worker) {
        uint64_t last_update = 0;
        unsigned last_percent = 101;
        uint32_t requested = (uint64_t)(end_ms - start_ms) * profile.rate / 1000;
        uint32_t maximum = (uint64_t)profile.max_ms * profile.rate / 1000;
        if (requested > maximum) requested = maximum;
        while (!job.done) {
            uint64_t now = osGetTime();
            uint32_t target = job.frames_target ? job.frames_target : requested;
            unsigned percent = target ? job.frames_done * 100u / target : 0;
            if (progress && (percent != last_percent || now - last_update >= 250)) {
                progress(percent);
                last_percent = percent;
                last_update = now;
            }
            hidScanInput();
            if ((hidKeysDown() & (KEY_B | KEY_Y)) || !aptMainLoop()) job.cancel = true;
            gspWaitForVBlank();
        }
        threadJoin(worker, U64_MAX);
        threadFree(worker);
    }
    ndspChnWaveBufClear(0);
    ndspChnReset(0);
    ndspExit();
    for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i)
        if (job.pcm[i]) linearFree(job.pcm[i]);
    aptSetSleepAllowed(true);
    if (cancelled) *cancelled = job.cancel;
    return worker && job.result;
}

/* A two-channel block cache keeps BCSTM preview reads sequential on SD. */
static bool read_pcm_frame(FILE *f, const BcstmInfo *info, uint32_t frame,
                           int16_t *cache, uint32_t *cached, int16_t out[2]) {
    uint32_t block = frame / info->block_frames;
    if (*cached != block) {
        uint32_t count = block + 1 == info->block_count ?
                         info->loop_end - block * info->block_frames : info->block_frames;
        uint32_t stride = block + 1 == info->block_count ?
                          info->last_padded : info->block_size;
        uint32_t base = info->samples_at + block * info->block_size * info->channels;
        for (unsigned channel = 0; channel < info->channels; ++channel) {
            if (fseek(f, base + channel * stride, SEEK_SET) ||
                fread(cache + channel * info->block_frames, 2, count, f) != count)
                return false;
        }
        *cached = block;
    }
    unsigned index = frame % info->block_frames;
    out[0] = cache[index];
    out[1] = info->channels == 2 ? cache[info->block_frames + index] : out[0];
    return true;
}

bool audio_preview_bcstm(const char *path, void (*progress)(unsigned percent),
                         bool *cancelled) {
    if (cancelled) *cancelled = false;
    BcstmInfo info;
    if (!bcstm_validate(path, &info) || info.encoding != 1 ||
        info.block_frames > 16384 || !info.block_frames) return false;
    uint32_t total = info.loop_end;
    if (total > info.sample_rate * 20u) total = info.sample_rate * 20u;
    int16_t *cache = malloc((size_t)info.block_frames * 4);
    FILE *f = fopen(path, "rb");
    int16_t *pcm[PREVIEW_BUFFERS] = {0};
    ndspWaveBuf wave[PREVIEW_BUFFERS];
    memset(wave, 0, sizeof(wave));
    bool ok = cache && f;
    for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i) {
        pcm[i] = linearAlloc(PREVIEW_BUFFER_FRAMES * 4);
        if (!pcm[i]) ok = false;
        wave[i].status = NDSP_WBUF_DONE;
    }
    bool sleep_blocked = false, audio_ready = false;
    if (ok) {
        aptSetSleepAllowed(false);
        sleep_blocked = true;
        ok = ndspInit() == 0;
        audio_ready = ok;
    }
    if (ok) {
        ndspChnReset(0);
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
        ndspChnSetRate(0, (float)info.sample_rate);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        float mix[12] = {0}; mix[0] = mix[1] = 1.0f;
        ndspChnSetMix(0, mix);
    }
    uint32_t cached = UINT32_MAX;
    uint32_t at = 0;
    unsigned slot = 0;
    bool stopped = false;
    while (ok && at < total && !stopped && aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_B | KEY_Y)) { stopped = true; break; }
        if (wave[slot].status != NDSP_WBUF_DONE) {
            gspWaitForVBlank();
            continue;
        }
        unsigned count = total - at < PREVIEW_BUFFER_FRAMES ?
                         total - at : PREVIEW_BUFFER_FRAMES;
        for (unsigned i = 0; i < count && ok; ++i)
            ok = read_pcm_frame(f, &info, at + i, cache, &cached, pcm[slot] + 2*i);
        if (!ok) break;
        wave[slot].data_vaddr = pcm[slot];
        wave[slot].nsamples = count;
        DSP_FlushDataCache(pcm[slot], count * 4);
        ndspChnWaveBufAdd(0, &wave[slot]);
        at += count;
        if (progress) progress(at * 100u / total);
        slot = (slot + 1) % PREVIEW_BUFFERS;
    }
    while (ok && at == total && !stopped && aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_B | KEY_Y)) { stopped = true; break; }
        bool playing = false;
        for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i)
            if (wave[i].status != NDSP_WBUF_DONE) playing = true;
        if (!playing) break;
        gspWaitForVBlank();
    }
    if (audio_ready) {
        ndspChnWaveBufClear(0);
        ndspChnReset(0);
        ndspExit();
    }
    if (sleep_blocked) aptSetSleepAllowed(true);
    if (f) fclose(f);
    free(cache);
    for (unsigned i = 0; i < PREVIEW_BUFFERS; ++i)
        if (pcm[i]) linearFree(pcm[i]);
    if (cancelled) *cancelled = stopped;
    return ok && at == total && !stopped;
}
