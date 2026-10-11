#ifndef AUDIO_NDSP_H
#define AUDIO_NDSP_H

/*
 * EB-Native S-DSP: the SNES sound chip (BRR decoding, gaussian interpolation, ADSR/GAIN envelopes,
 * noise, pitch modulation, echo + FIR, master mix), implemented in this tree. The SPC700 CPU
 * that drives it is still emulated (lakesnes); see docs/eb-native-audio.md.
 *
 * Contract: one ndsp_cycle() per output sample (every 32 SPC700 cycles, 32 kHz), register access
 * through ndsp_write()/ndsp_read() (the SPC700's $F3 port), and the 64 KiB audio RAM shared with
 * the SPC700 (BRR samples, sample directory, echo buffer). Behaviour is verified bit-exact
 * against lakesnes' DSP (`--selftest-audio`).
 *
 * The hardware gaussian table is data derived from the S-DSP ROM; it was taken from lakesnes
 * (MIT, (c) angelo_wf and contributors), see src/vendor/lakesnes/LICENSE.txt.
 */

#include <stdint.h>
#include <stdbool.h>

#define NDSP_RING 1024 /* output ring, stereo frames; same size lakesnes uses */

typedef struct NDspVoice {
    uint16_t pitch;          /* 14-bit */
    uint16_t pitch_counter;
    bool     pitch_mod;
    int16_t  brr[12];        /* decoded samples, three 4-sample groups in a ring */
    uint8_t  brr_pos;        /* ring write position (0, 4, 8) */
    uint8_t  srcn;
    uint16_t brr_addr;       /* address of the current BRR block */
    uint8_t  brr_nibble;     /* byte offset inside the block (1..7) */
    uint8_t  brr_header;
    bool     noise;
    uint8_t  start_delay;    /* key-on pipeline (5 .. 0) */
    uint8_t  rate[4];        /* attack, decay, sustain, gain (already mapped to the 0..31 rate index) */
    uint8_t  env_state;      /* 0 attack, 1 decay, 2 sustain, 3 release */
    uint8_t  sustain_level;
    uint8_t  gain_sustain_level;
    bool     use_gain;       /* ADSR1 bit 7 clear: GAIN register controls the envelope */
    uint8_t  gain_mode;
    bool     direct_gain;
    uint16_t gain_direct;
    uint16_t gain_preclamp;
    uint16_t gain;           /* 11-bit envelope */
    bool     key_on, key_off;
    int16_t  out;            /* voice output before volume (also feeds pitch modulation) */
    int8_t   vol_l, vol_r;
    bool     echo;
} NDspVoice;

typedef struct NDsp {
    uint8_t *ram;            /* shared 64 KiB audio RAM (not owned) */
    bool     echo_write_locked; /* verification only: drop echo-buffer writes (the reference DSP does them) */
    uint8_t  regs[0x80];
    NDspVoice v[8];
    uint16_t voice_gain[8];  /* host output gain per voice, Q8 (256 = 1.0); not hardware state */
    uint16_t counter;        /* global rate counter, counts 30720 .. 0 */
    uint16_t dir_page;
    bool     even;
    bool     mute, reset;
    int8_t   main_l, main_r;
    int16_t  mix_l, mix_r, echo_in_l, echo_in_r;
    int16_t  noise;
    uint8_t  noise_rate;
    bool     echo_writes;
    int8_t   echo_vol_l, echo_vol_r, feedback;
    uint16_t echo_base, echo_delay, echo_len, echo_pos;
    uint8_t  fir_pos;
    int8_t   fir[8];
    int16_t  fir_l[8], fir_r[8];
    int16_t  ring[NDSP_RING * 2];
    uint16_t ring_pos;
} NDsp;

void    ndsp_init(NDsp *d, uint8_t *ram64k);
void    ndsp_reset(NDsp *d);
void    ndsp_cycle(NDsp *d);
void    ndsp_write(NDsp *d, uint8_t reg, uint8_t val);
uint8_t ndsp_read(const NDsp *d, uint8_t reg);
/* Resamples the most recent 534-sample frame (NTSC) of the ring into `n` stereo frames. */
void    ndsp_get_samples(const NDsp *d, int16_t *out, int n);

#endif
