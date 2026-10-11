#ifndef AUDIO_HQMUSIC_H
#define AUDIO_HQMUSIC_H

/*
 * Optional replacement music (disabled by default).
 *
 * A directory (the same one given to --mods) may contain `music/NNN.wav`, NNN = the game's music
 * track number with three digits (change_music() id, e.g. 004.wav). When the option is on and the
 * file passes validation, that recording plays instead of the SPC700 music for the track; the
 * original sequence keeps running silently so sound effects and the sound driver stay in step.
 * Any problem (no file, bad file, option off) leaves the original music untouched.
 *
 * Format: RIFF/WAVE, PCM, 16-bit, 1 or 2 channels, 8000..192000 Hz, at most 10 minutes. It is
 * resampled to the 32 kHz output rate when loaded. The whole file loops; an optional `NNN.loop`
 * text file holds the loop start in source sample frames. Fades: 250 ms in, 250 ms out on changes.
 * No music is shipped with the project: supply your own recordings.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct HqStream HqStream;

/* Parse and validate a WAV image. On success returns a stream resampled to 32 kHz stereo; on
 * failure returns NULL and writes a reason into err. `loop_src_frames` is the loop start in
 * source frames (0 = start). */
HqStream *hqmusic_parse_wav(const uint8_t *data, size_t size, size_t loop_src_frames, char *err, size_t err_len);
/* Load <dir>/music/NNN.wav (+ NNN.loop). NULL if absent or invalid (reason on stderr only when the
 * file exists but is rejected). Does file I/O: call outside the audio lock. */
HqStream *hqmusic_load(const char *dir, int track);
void      hqmusic_free(HqStream *s);
size_t    hqmusic_frames(const HqStream *s);

/* Audio-thread state (call with the audio lock held). */
void hqmusic_switch(HqStream *next);   /* take ownership; current stream (if any) fades out */
bool hqmusic_active(void);
/* Add `n` stereo frames of the stream(s), scaled by gain_q8 (256 = 1.0), into buf. */
void hqmusic_mix(int16_t *buf, int n, int gain_q8);
void hqmusic_shutdown(void);
bool hqmusic_selftest(void); /* parser validation + fade/loop/mix behaviour, no files needed */

#endif
