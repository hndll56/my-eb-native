#include "audio/hqmusic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE 32000
#define MAX_SECONDS 600
#define FADE_FRAMES (OUT_RATE / 4)

struct HqStream {
    int16_t *pcm;        /* stereo, 32 kHz */
    size_t   frames;
    size_t   loop_start; /* output frames */
    size_t   pos;
    size_t   fade_pos;   /* frames faded so far */
    bool     fading_out;
};

static HqStream *cur, *old;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void fail(char *err, size_t n, const char *msg) { if (err && n) snprintf(err, n, "%s", msg); }

HqStream *hqmusic_parse_wav(const uint8_t *d, size_t size, size_t loop_src, char *err, size_t en) {
    if (!d || size < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { fail(err, en, "not a RIFF/WAVE file"); return NULL; }
    uint16_t fmt_tag = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *pcm = NULL;
    size_t pcm_bytes = 0;
    bool have_fmt = false;
    size_t p = 12;
    while (p + 8 <= size) {
        uint32_t csz = rd32(d + p + 4);
        const uint8_t *body = d + p + 8;
        size_t avail = size - (p + 8);
        if (!memcmp(d + p, "fmt ", 4)) {
            if (csz < 16 || avail < 16) { fail(err, en, "truncated fmt chunk"); return NULL; }
            fmt_tag = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
            have_fmt = true;
        } else if (!memcmp(d + p, "data", 4)) {
            pcm = body;
            pcm_bytes = csz < avail ? csz : avail; /* tolerate a data size that overshoots the file */
            break;
        }
        if (csz > avail) break;
        p += 8 + (size_t)csz + (csz & 1);
    }
    if (!have_fmt || !pcm) { fail(err, en, "missing fmt or data chunk"); return NULL; }
    if (fmt_tag != 1) { fail(err, en, "only PCM is supported"); return NULL; }
    if (bits != 16) { fail(err, en, "only 16-bit samples are supported"); return NULL; }
    if (ch < 1 || ch > 2) { fail(err, en, "only mono or stereo is supported"); return NULL; }
    if (rate < 8000 || rate > 192000) { fail(err, en, "sample rate outside 8000..192000"); return NULL; }
    size_t src_frames = pcm_bytes / (size_t)(2 * ch);
    if (src_frames < 2) { fail(err, en, "no audio data"); return NULL; }
    if (src_frames > (size_t)rate * MAX_SECONDS) { fail(err, en, "longer than 10 minutes"); return NULL; }
    if (loop_src >= src_frames) loop_src = 0;

    size_t out_frames = (size_t)((uint64_t)src_frames * OUT_RATE / rate);
    if (out_frames < 2) { fail(err, en, "too short"); return NULL; }
    HqStream *s = calloc(1, sizeof(*s));
    int16_t *o = malloc(out_frames * 2 * sizeof(int16_t));
    if (!s || !o) { free(s); free(o); fail(err, en, "out of memory"); return NULL; }
    #define SMP(frame, c) ((int)(int16_t)rd16(pcm + ((frame) * (size_t)ch + ((c) < ch ? (c) : 0)) * 2))
    for (size_t i = 0; i < out_frames; i++) {
        uint64_t num = (uint64_t)i * rate;
        size_t i0 = (size_t)(num / OUT_RATE);
        unsigned frac = (unsigned)((num % OUT_RATE) * 256 / OUT_RATE);
        size_t i1 = i0 + 1 < src_frames ? i0 + 1 : i0;
        for (int c = 0; c < 2; c++) {
            int a = SMP(i0, c), b = SMP(i1, c);
            o[i * 2 + c] = (int16_t)(a + (((b - a) * (int)frac) >> 8));
        }
    }
    #undef SMP
    s->pcm = o;
    s->frames = out_frames;
    s->loop_start = (size_t)((uint64_t)loop_src * OUT_RATE / rate);
    if (s->loop_start >= out_frames) s->loop_start = 0;
    return s;
}

void hqmusic_free(HqStream *s) { if (s) { free(s->pcm); free(s); } }
size_t hqmusic_frames(const HqStream *s) { return s ? s->frames : 0; }

static uint8_t *slurp(const char *path, size_t *size, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && (size_t)n <= cap && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)n);
            if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
            else if (buf) *size = (size_t)n;
        } else if (n > 0) {
            fprintf(stderr, "hq music: %s is larger than %zu bytes, ignored\n", path, cap);
            fclose(f);
            return NULL;
        }
    }
    fclose(f);
    return buf;
}

HqStream *hqmusic_load(const char *dir, int track) {
    if (!dir || track < 0 || track > 999) return NULL;
    char path[1100];
    snprintf(path, sizeof(path), "%s/music/%03d.wav", dir, track);
    size_t size = 0;
    uint8_t *img = slurp(path, &size, (size_t)192000 * 4 * MAX_SECONDS / 4 + 4096);
    if (!img) return NULL;
    size_t loop = 0;
    snprintf(path, sizeof(path), "%s/music/%03d.loop", dir, track);
    FILE *lf = fopen(path, "r");
    if (lf) { long v = 0; if (fscanf(lf, "%ld", &v) == 1 && v > 0) loop = (size_t)v; fclose(lf); }
    char err[96] = "";
    HqStream *s = hqmusic_parse_wav(img, size, loop, err, sizeof(err));
    free(img);
    if (!s) fprintf(stderr, "hq music: track %03d.wav rejected (%s); using the original music\n", track, err);
    return s;
}

void hqmusic_switch(HqStream *next) {
    if (old) { hqmusic_free(old); old = NULL; }
    if (cur) { cur->fading_out = true; cur->fade_pos = 0; old = cur; }
    cur = next;
    if (cur) { cur->pos = 0; cur->fade_pos = 0; cur->fading_out = false; }
}

bool hqmusic_active(void) { return cur != NULL; }

static bool mix_stream(HqStream *s, int16_t *buf, int n, int gain_q8) {
    for (int i = 0; i < n; i++) {
        int g = gain_q8;
        if (s->fading_out) {
            if (s->fade_pos >= FADE_FRAMES) return false;
            g = (int)((int64_t)g * (FADE_FRAMES - s->fade_pos) / FADE_FRAMES);
            s->fade_pos++;
        } else if (s->fade_pos < FADE_FRAMES) {
            g = (int)((int64_t)g * s->fade_pos / FADE_FRAMES);
            s->fade_pos++;
        }
        for (int c = 0; c < 2; c++) {
            int v = buf[i * 2 + c] + ((s->pcm[s->pos * 2 + c] * g) >> 8);
            buf[i * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (++s->pos >= s->frames) s->pos = s->loop_start;
    }
    return true;
}

void hqmusic_mix(int16_t *buf, int n, int gain_q8) {
    if (old && !mix_stream(old, buf, n, gain_q8)) { hqmusic_free(old); old = NULL; }
    if (cur) mix_stream(cur, buf, n, gain_q8);
}

void hqmusic_shutdown(void) {
    hqmusic_free(cur); hqmusic_free(old);
    cur = old = NULL;
}

/* ---- self-test: synthetic WAV images, no files or devices needed ---- */
static size_t make_wav(uint8_t *o, int rate, int ch, int bits, int tag, int frames, int freq, int amp) {
    size_t data = (size_t)frames * ch * (bits / 8);
    memcpy(o, "RIFF", 4); o[4] = (uint8_t)(36 + data); o[5] = (uint8_t)((36 + data) >> 8); o[6] = (uint8_t)((36 + data) >> 16); o[7] = 0;
    memcpy(o + 8, "WAVEfmt ", 8);
    o[16] = 16; o[17] = o[18] = o[19] = 0;
    o[20] = (uint8_t)tag; o[21] = 0; o[22] = (uint8_t)ch; o[23] = 0;
    o[24] = (uint8_t)rate; o[25] = (uint8_t)(rate >> 8); o[26] = (uint8_t)(rate >> 16); o[27] = 0;
    uint32_t br = (uint32_t)rate * ch * (bits / 8);
    o[28] = (uint8_t)br; o[29] = (uint8_t)(br >> 8); o[30] = (uint8_t)(br >> 16); o[31] = (uint8_t)(br >> 24);
    o[32] = (uint8_t)(ch * (bits / 8)); o[33] = 0; o[34] = (uint8_t)bits; o[35] = 0;
    memcpy(o + 36, "data", 4);
    o[40] = (uint8_t)data; o[41] = (uint8_t)(data >> 8); o[42] = (uint8_t)(data >> 16); o[43] = (uint8_t)(data >> 24);
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++) {
            int v = (int)(amp * ((i * freq * 2 / rate) & 1 ? 1 : -1)); /* square wave, easy to reason about */
            if (bits == 16) { o[44 + (i * ch + c) * 2] = (uint8_t)v; o[45 + (i * ch + c) * 2] = (uint8_t)(v >> 8); }
        }
    return 44 + data;
}

bool hqmusic_selftest(void) {
    bool ok = true;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "hq music selftest: failed: %s\n", #c); ok = false; } } while (0)
    static uint8_t w[44 + 44100 * 4];
    char err[96];
    size_t n = make_wav(w, 44100, 2, 16, 1, 44100, 440, 8000);
    HqStream *s = hqmusic_parse_wav(w, n, 0, err, sizeof(err));
    CHECK(s != NULL);
    if (s) {
        CHECK(hqmusic_frames(s) >= 31990 && hqmusic_frames(s) <= 32010);   /* 1 s of 44.1 kHz -> ~32000 frames */
        hqmusic_free(s);
    }
    n = make_wav(w, 16000, 1, 16, 1, 16000, 200, 8000);                    /* mono, 16 kHz: upsampled */
    s = hqmusic_parse_wav(w, n, 0, err, sizeof(err));
    CHECK(s && hqmusic_frames(s) >= 31990 && hqmusic_frames(s) <= 32010);
    hqmusic_free(s);

    /* rejected inputs must return NULL with a reason, never crash */
    CHECK(!hqmusic_parse_wav(w, 10, 0, err, sizeof(err)));
    CHECK(!hqmusic_parse_wav(NULL, 0, 0, err, sizeof(err)));
    n = make_wav(w, 44100, 2, 16, 3, 100, 440, 1000);  CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err)));  /* float tag */
    n = make_wav(w, 44100, 6, 16, 1, 100, 440, 1000);  CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err)));  /* 6 channels */
    n = make_wav(w, 100, 2, 16, 1, 100, 440, 1000);    CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err)));  /* 100 Hz */
    n = make_wav(w, 44100, 2, 16, 1, 100, 440, 1000);  w[34] = 24; CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err))); /* 24-bit */
    n = make_wav(w, 44100, 2, 16, 1, 100, 440, 1000);  memcpy(w, "RIFX", 4); CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err)));
    n = make_wav(w, 44100, 2, 16, 1, 100, 440, 1000);  w[40] = w[41] = w[42] = 0xFF; w[43] = 0x7F;           /* data size lies */
    s = hqmusic_parse_wav(w, n, 0, err, sizeof(err));  CHECK(s != NULL); hqmusic_free(s);                       /* clamped to file size */
    n = make_wav(w, 44100, 2, 16, 1, 1, 440, 1000);    CHECK(!hqmusic_parse_wav(w, n, 0, err, sizeof(err)));  /* 1 frame */

    /* playback: fade-in from silence, looping, fade-out on switch, additive mix */
    n = make_wav(w, 32000, 2, 16, 1, 1000, 100, 10000);
    s = hqmusic_parse_wav(w, n, 500, err, sizeof(err));
    CHECK(s != NULL);
    hqmusic_switch(s);
    CHECK(hqmusic_active());
    static int16_t buf[2 * 9000];
    memset(buf, 0, sizeof(buf));
    hqmusic_mix(buf, 9000, 256);
    int early = 0, late = 0;
    for (int i = 0; i < 100; i++) early += abs(buf[i * 2]);
    for (int i = 8000; i < 8100; i++) late += abs(buf[i * 2]);
    CHECK(early < late / 4);                          /* faded in */
    CHECK(late > 100 * 9000);                         /* full level reached (8 000 frames = fade length) */
    memset(buf, 0, sizeof(buf));
    hqmusic_switch(NULL);                             /* old stream fades out and then disappears */
    CHECK(!hqmusic_active());
    hqmusic_mix(buf, 9000, 256);
    int a = 0, b = 0;
    for (int i = 0; i < 100; i++) a += abs(buf[i * 2]);
    for (int i = 7900; i < 8000; i++) b += abs(buf[i * 2]);
    CHECK(a > 100 * 5000 && b < a / 10);
    memset(buf, 0, sizeof(buf));
    hqmusic_mix(buf, 100, 256);                       /* nothing left to mix */
    for (int i = 0; i < 200; i++) CHECK(buf[i] == 0);
    hqmusic_shutdown();
#undef CHECK
    return ok;
}
