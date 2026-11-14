#include "bcstm_validate.h"
#include <stdio.h>
#include <string.h>

#define MAX_BCSTM_SIZE 0x337000u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool read_at(FILE *f, uint32_t at, void *p, size_t n) {
    return fseek(f, (long)at, SEEK_SET) == 0 && fread(p, 1, n, f) == n;
}
static bool range(uint32_t at, uint32_t n, uint32_t size) {
    return at <= size && n <= size - at;
}

bool bcstm_validate(const char *path, BcstmInfo *result) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    uint8_t h[0x40], b[0x38], stream[0x38];
    uint32_t info_at = 0, info_size = 0, data_at = 0, data_size = 0;
    uint32_t seek_at = 0, seek_size = 0;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) goto done;
    long end = ftell(f);
    if (end < 0xe0 || (unsigned long)end > MAX_BCSTM_SIZE || !read_at(f, 0, h, sizeof(h))) goto done;
    uint32_t size = (uint32_t)end;
    if (memcmp(h, "CSTM", 4) || rd16(h+4) != 0xfeff || rd16(h+6) != 0x40 ||
        rd32(h+0xc) != size || rd16(h+0x10) != 3) goto done;
    unsigned blocks = rd16(h+0x10);
    for (unsigned i = 0; i < blocks; ++i) {
        const uint8_t *ref = h + 0x14 + i * 12;
        uint16_t type = rd16(ref);
        uint32_t at = rd32(ref+4), length = rd32(ref+8);
        if ((at & 31) || length < 8 || !range(at, length, size)) goto done;
        if (type == 0x4000) { if (info_at) goto done; info_at = at; info_size = length; }
        else if (type == 0x4001) { if (seek_at) goto done; seek_at = at; seek_size = length; }
        else if (type == 0x4002) { if (data_at) goto done; data_at = at; data_size = length; }
        else goto done;
    }
    if (!info_at || !seek_at || !data_at || info_size < 0xa0 ||
        seek_size < 8 || data_size < 0x20 ||
        seek_at < info_at + info_size || data_at < seek_at + seek_size ||
        data_at + data_size != size) goto done;
    if (!read_at(f, info_at, b, sizeof(b)) || memcmp(b, "INFO", 4) || rd32(b+4) != info_size ||
        rd16(b+8) != 0x4100 || rd16(b+0x10) != 0x0101 ||
        rd16(b+0x18) != 0x0101) goto done;
    uint32_t stream_at = info_at + 8 + rd32(b+0xc);
    uint32_t tracks_at = info_at + 8 + rd32(b+0x14);
    uint32_t channels_at = info_at + 8 + rd32(b+0x1c);
    if (!range(stream_at, sizeof(stream), info_at + info_size) ||
        !range(tracks_at, 12, info_at + info_size) ||
        !range(channels_at, 4, info_at + info_size) ||
        !read_at(f, stream_at, stream, sizeof(stream))) goto done;
    uint8_t codec = stream[0], looping = stream[1], channels = stream[2];
    uint32_t rate = rd32(stream+4), start = rd32(stream+8), frames = rd32(stream+0xc);
    uint32_t block_count = rd32(stream+0x10), block_size = rd32(stream+0x14);
    uint32_t block_frames = rd32(stream+0x18), last_size = rd32(stream+0x1c);
    uint32_t last_frames = rd32(stream+0x20), last_padded = rd32(stream+0x24);
    if ((codec != 1 && codec != 2) || channels < 1 || channels > 2 || looping > 1 ||
        rate < 8000 || rate > 48000 || !frames || start >= frames ||
        !block_count || !block_frames || !last_frames || last_frames > block_frames ||
        frames != (uint64_t)(block_count-1)*block_frames + last_frames ||
        !block_size || !last_size || last_padded < last_size || (last_padded & 31) ||
        (codec == 1 && (block_size != block_frames*2u || last_size != last_frames*2u)) ||
        (codec == 2 && (block_size*14u != block_frames*8u))) goto done;
    uint64_t sample_bytes = (uint64_t)channels * ((uint64_t)(block_count-1)*block_size + last_padded);
    if (rd16(stream+0x30) != 0x1f00 || rd32(stream+0x34) != 0x18 ||
        sample_bytes != data_size - 0x20 ||
        (uint64_t)seek_size < 8u + (uint64_t)block_count * channels * rd32(stream+0x28)) goto done;
    uint32_t samples_at = data_at + 8 + rd32(stream+0x34);
    uint8_t data_h[8], count_h[4], channel_refs[16], track_ref[8], track[12];
    if (!read_at(f, data_at, data_h, sizeof(data_h)) || memcmp(data_h, "DATA", 4) ||
        rd32(data_h+4) != data_size || !range(samples_at, (uint32_t)sample_bytes, size) ||
        !read_at(f, channels_at, count_h, 4) || rd32(count_h) != channels ||
        !range(channels_at, 4 + channels * 16u, info_at + info_size) ||
        !read_at(f, channels_at + 4, channel_refs, channels * 8u) ||
        !read_at(f, tracks_at, count_h, 4) || rd32(count_h) != 1 ||
        !range(tracks_at, 12, info_at + info_size) ||
        !read_at(f, tracks_at + 4, track_ref, 8) ||
        rd16(track_ref) != 0x4101) goto done;
    uint32_t track_at = tracks_at + rd32(track_ref+4);
    if (!range(track_at, sizeof(track), info_at + info_size) ||
        !read_at(f, track_at, track, sizeof(track)) ||
        rd16(track+4) != 0x0100) goto done;
    uint32_t indices_at = track_at + rd32(track+8);
    if (!range(indices_at, 4u + channels, info_at + info_size) ||
        !read_at(f, indices_at, count_h, 4) || rd32(count_h) != channels) goto done;
    uint8_t channel_ids[2];
    if (!read_at(f, indices_at + 4, channel_ids, channels)) goto done;
    for (unsigned i = 0; i < channels; ++i) if (channel_ids[i] != i) goto done;
    for (unsigned i = 0; i < channels; ++i) {
        uint32_t entry_at = channels_at + rd32(channel_refs + i*8 + 4);
        if (rd16(channel_refs + i*8) != 0x4102 ||
            !range(entry_at, 8, info_at + info_size)) goto done;
    }
    if (!read_at(f, seek_at, data_h, sizeof(data_h)) || memcmp(data_h, "SEEK", 4) ||
        rd32(data_h+4) != seek_size) goto done;
    if (result) *result = (BcstmInfo){rate, start, frames, size, codec, channels, looping};
    ok = true;
done:
    fclose(f);
    return ok;
}
