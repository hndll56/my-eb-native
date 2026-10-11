/* Deterministic audio equivalence test: drives the real sound driver (music packs + SFX commands)
 * without a sound device and checks the native DSP against lakesnes' DSP. See docs/eb-native-audio.md. */
#ifdef EB_ENABLE_AUDIO
#include "audio/audio_selftest.h"
#include "game/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SAMPLES 534

static uint64_t fnv(uint64_t h, const int16_t *p, int n) {
    for (int i = 0; i < n; i++) { h ^= (uint16_t)p[i]; h *= 1099511628211ull; }
    return h;
}

/* Plays one scenario: a music track, with a sound effect every 60 frames. Returns false if the
 * shadow DSP ever disagreed. `hashes` (optional, `frames` entries) receives a hash of each output frame. */
static unsigned long g_audible; /* frames with non-zero output, across all runs */

static bool play(int track, int frames, uint64_t *hashes) {
    audio_init();
    if (track & 1) audio_set_volumes(100, 50, 30); else audio_set_volumes(80, 100, 100); /* exercise the per-voice gain */
    change_music((uint16_t)track);
    int16_t buf[FRAME_SAMPLES * 2];
    for (int f = 0; f < frames; f++) {
        if (f % 60 == 30) play_sfx((uint16_t)(1 + (f / 60 * 7 + track) % 160));
        audio_process_sfx_queue();
        audio_generate_samples(buf, FRAME_SAMPLES);
        if (hashes) hashes[f] = fnv(14695981039346656037ull, buf, FRAME_SAMPLES * 2);
        for (int i = 0; i < FRAME_SAMPLES * 2; i++) if (buf[i]) { g_audible++; break; }
    }
    bool ok = true;
    if (audio_dsp_mode == AUDIO_DSP_SHADOW) {
        const AudioShadowStats *st = audio_shadow_stats();
        if (st->bad_frames) {
            fprintf(stderr, "audio selftest: track %d: native DSP differs in %lu of %lu frames (first: frame %lu, ring index %d)\n",
                    track, st->bad_frames, st->frames, st->first_bad_frame, st->first_bad_index);
            ok = false;
        }
    }
    audio_shutdown();
    audio_set_volumes(100, 100, 100);
    return ok;
}

bool audio_selftest(int first_track, int last_track, int frames) {
    if (frames < 1) frames = 1;
    audio_init(); /* the track count comes from the dataset table loaded by audio_init() */
    int tracks = audio_track_count();
    audio_shutdown();
    if (first_track < 1) first_track = 1;
    if (last_track > tracks) last_track = tracks;
    if (last_track < first_track) {
        fprintf(stderr, "audio selftest: no tracks to test (dataset has %d)\n", tracks);
        return false;
    }
    uint64_t *a = malloc((size_t)frames * sizeof(*a)), *b = malloc((size_t)frames * sizeof(*b));
    if (!a || !b) { free(a); free(b); return false; }
    bool ok = true;
    int shadow_ok = 0, out_ok = 0, n = 0;
    for (int t = first_track; t <= last_track; t++, n++) {
        audio_dsp_mode = AUDIO_DSP_SHADOW;
        bool s = play(t, frames, NULL);
        audio_dsp_mode = AUDIO_DSP_LAKESNES;
        play(t, frames, a);
        audio_dsp_mode = AUDIO_DSP_NATIVE;
        play(t, frames, b);
        bool o = memcmp(a, b, (size_t)frames * sizeof(*a)) == 0;
        if (!o) {
            int bad = 0;
            while (bad < frames && a[bad] == b[bad]) bad++;
            fprintf(stderr, "audio selftest: track %d: native-mode output differs from lakesnes output (first frame %d)\n", t, bad);
        }
        shadow_ok += s;
        out_ok += o;
        ok = ok && s && o;
    }
    audio_dsp_mode = AUDIO_DSP_LAKESNES;
    if (g_audible == 0) { fprintf(stderr, "audio selftest: every frame was silent, nothing was verified\n"); ok = false; }
    fprintf(stderr, "audio selftest: %lu audible frames in total\n", g_audible);
    fprintf(stderr, "audio selftest: %d track(s) x %d frames: shadow identical %d/%d, native output identical %d/%d\n",
            n, frames, shadow_ok, n, out_ok, n);
    free(a); free(b);
    return ok;
}
#endif
