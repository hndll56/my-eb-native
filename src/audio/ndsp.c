/* EB-Native S-DSP. See ndsp.h. */
#include "audio/ndsp.h"
#include <string.h>

static const int RATE_PERIOD[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1
};
static const int RATE_PHASE[32] = {
    0, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
    0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0
};

static const int GAUSS[512] = {
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
  0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
  0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00a, 0x00a, 0x00a,
  0x00b, 0x00b, 0x00b, 0x00c, 0x00c, 0x00d, 0x00d, 0x00e, 0x00e, 0x00f, 0x00f, 0x00f, 0x010, 0x010, 0x011, 0x011,
  0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01a, 0x01b, 0x01b,
  0x01c, 0x01d, 0x01d, 0x01e, 0x01f, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
  0x029, 0x02a, 0x02b, 0x02c, 0x02d, 0x02e, 0x02f, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
  0x03a, 0x03b, 0x03c, 0x03d, 0x03e, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04a, 0x04c, 0x04d,
  0x04e, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05a, 0x05c, 0x05e, 0x05f, 0x061, 0x063, 0x064, 0x066,
  0x068, 0x06a, 0x06b, 0x06d, 0x06f, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07a, 0x07c, 0x07e, 0x080, 0x082, 0x084,
  0x086, 0x089, 0x08b, 0x08d, 0x08f, 0x091, 0x093, 0x096, 0x098, 0x09a, 0x09c, 0x09f, 0x0a1, 0x0a3, 0x0a6, 0x0a8,
  0x0ab, 0x0ad, 0x0af, 0x0b2, 0x0b4, 0x0b7, 0x0ba, 0x0bc, 0x0bf, 0x0c1, 0x0c4, 0x0c7, 0x0c9, 0x0cc, 0x0cf, 0x0d2,
  0x0d4, 0x0d7, 0x0da, 0x0dd, 0x0e0, 0x0e3, 0x0e6, 0x0e9, 0x0ec, 0x0ef, 0x0f2, 0x0f5, 0x0f8, 0x0fb, 0x0fe, 0x101,
  0x104, 0x107, 0x10b, 0x10e, 0x111, 0x114, 0x118, 0x11b, 0x11e, 0x122, 0x125, 0x129, 0x12c, 0x130, 0x133, 0x137,
  0x13a, 0x13e, 0x141, 0x145, 0x148, 0x14c, 0x150, 0x153, 0x157, 0x15b, 0x15f, 0x162, 0x166, 0x16a, 0x16e, 0x172,
  0x176, 0x17a, 0x17d, 0x181, 0x185, 0x189, 0x18d, 0x191, 0x195, 0x19a, 0x19e, 0x1a2, 0x1a6, 0x1aa, 0x1ae, 0x1b2,
  0x1b7, 0x1bb, 0x1bf, 0x1c3, 0x1c8, 0x1cc, 0x1d0, 0x1d5, 0x1d9, 0x1dd, 0x1e2, 0x1e6, 0x1eb, 0x1ef, 0x1f3, 0x1f8,
  0x1fc, 0x201, 0x205, 0x20a, 0x20f, 0x213, 0x218, 0x21c, 0x221, 0x226, 0x22a, 0x22f, 0x233, 0x238, 0x23d, 0x241,
  0x246, 0x24b, 0x250, 0x254, 0x259, 0x25e, 0x263, 0x267, 0x26c, 0x271, 0x276, 0x27b, 0x280, 0x284, 0x289, 0x28e,
  0x293, 0x298, 0x29d, 0x2a2, 0x2a6, 0x2ab, 0x2b0, 0x2b5, 0x2ba, 0x2bf, 0x2c4, 0x2c9, 0x2ce, 0x2d3, 0x2d8, 0x2dc,
  0x2e1, 0x2e6, 0x2eb, 0x2f0, 0x2f5, 0x2fa, 0x2ff, 0x304, 0x309, 0x30e, 0x313, 0x318, 0x31d, 0x322, 0x326, 0x32b,
  0x330, 0x335, 0x33a, 0x33f, 0x344, 0x349, 0x34e, 0x353, 0x357, 0x35c, 0x361, 0x366, 0x36b, 0x370, 0x374, 0x379,
  0x37e, 0x383, 0x388, 0x38c, 0x391, 0x396, 0x39b, 0x39f, 0x3a4, 0x3a9, 0x3ad, 0x3b2, 0x3b7, 0x3bb, 0x3c0, 0x3c5,
  0x3c9, 0x3ce, 0x3d2, 0x3d7, 0x3dc, 0x3e0, 0x3e5, 0x3e9, 0x3ed, 0x3f2, 0x3f6, 0x3fb, 0x3ff, 0x403, 0x408, 0x40c,
  0x410, 0x415, 0x419, 0x41d, 0x421, 0x425, 0x42a, 0x42e, 0x432, 0x436, 0x43a, 0x43e, 0x442, 0x446, 0x44a, 0x44e,
  0x452, 0x455, 0x459, 0x45d, 0x461, 0x465, 0x468, 0x46c, 0x470, 0x473, 0x477, 0x47a, 0x47e, 0x481, 0x485, 0x488,
  0x48c, 0x48f, 0x492, 0x496, 0x499, 0x49c, 0x49f, 0x4a2, 0x4a6, 0x4a9, 0x4ac, 0x4af, 0x4b2, 0x4b5, 0x4b7, 0x4ba,
  0x4bd, 0x4c0, 0x4c3, 0x4c5, 0x4c8, 0x4cb, 0x4cd, 0x4d0, 0x4d2, 0x4d5, 0x4d7, 0x4d9, 0x4dc, 0x4de, 0x4e0, 0x4e3,
  0x4e5, 0x4e7, 0x4e9, 0x4eb, 0x4ed, 0x4ef, 0x4f1, 0x4f3, 0x4f5, 0x4f6, 0x4f8, 0x4fa, 0x4fb, 0x4fd, 0x4ff, 0x500,
  0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50a, 0x50b, 0x50c, 0x50d, 0x50e, 0x50f, 0x510, 0x511, 0x511, 0x512,
  0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};

enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
enum { REG_ENVX = 0x08, REG_OUTX = 0x09, REG_ENDX = 0x7c };

static int sat16(int v) { return v < -0x8000 ? -0x8000 : v > 0x7fff ? 0x7fff : v; }
static int wrap16(int v) { return (int16_t)(v & 0xffff); }

/* True on the samples where an envelope/noise generator with this rate index steps. */
static bool rate_hit(const NDsp *d, int rate) {
    return rate != 0 && ((d->counter + RATE_PHASE[rate]) % RATE_PERIOD[rate]) == 0;
}

void ndsp_init(NDsp *d, uint8_t *ram64k) {
    memset(d, 0, sizeof(*d));
    d->ram = ram64k;
    ndsp_reset(d);
}

void ndsp_reset(NDsp *d) {
    uint8_t *ram = d->ram;
    bool lock = d->echo_write_locked;
    memset(d, 0, sizeof(*d));
    d->ram = ram;
    d->echo_write_locked = lock;
    d->regs[REG_ENDX] = 0xff;
    for (int i = 0; i < 8; i++) d->voice_gain[i] = 256;
    d->even = true;
    d->mute = true;
    d->reset = true;
    d->noise = 0x4000;
}

static int16_t ram16(const NDsp *d, unsigned addr) {
    return (int16_t)(uint16_t)(d->ram[addr & 0xffff] | (d->ram[(addr + 1) & 0xffff] << 8));
}

/* Decodes one 4-sample BRR group (two bytes of the block) into the voice's ring. */
static void brr_decode_group(NDsp *d, NDspVoice *v) {
    int shift = v->brr_header >> 4;
    int filter = (v->brr_header & 0x0c) >> 2;
    int pos = v->brr_pos;
    int p1 = v->brr[pos == 0 ? 11 : pos - 1] >> 1;
    int p2 = v->brr[pos == 0 ? 10 : pos - 2] >> 1;
    uint8_t byte = 0;
    for (int i = 0; i < 4; i++) {
        int s;
        if (i & 1) {
            s = byte & 0x0f;
        } else {
            byte = d->ram[(v->brr_addr + v->brr_nibble + (i >> 1)) & 0xffff];
            s = byte >> 4;
        }
        if (s > 7) s -= 16;
        s = shift <= 12 ? (int)((unsigned)s << shift) >> 1 : (int)((unsigned)(s >> 3) << 12);
        switch (filter) {
        case 1: s += p1 + (-p1 >> 4); break;
        case 2: s += 2 * p1 + ((3 * -p1) >> 5) - p2 + (p2 >> 4); break;
        case 3: s += 2 * p1 + ((13 * -p1) >> 6) - p2 + ((3 * p2) >> 4); break;
        }
        v->brr[pos + i] = (int16_t)(sat16(s) * 2); /* bit 15 is dropped by the hardware */
        p2 = p1;
        p1 = v->brr[pos + i] >> 1;
    }
    v->brr_pos += 4;
    if (v->brr_pos >= 12) v->brr_pos = 0;
}

/* 4-point gaussian interpolation of the decoded ring at the voice's fractional position. */
static int16_t interpolate(const NDspVoice *v) {
    int pos = (v->pitch_counter >> 12) + v->brr_pos;
    int frac = (v->pitch_counter >> 4) & 0xff;
    int i0 = pos, i1 = pos + 1, i2 = pos + 2, i3 = pos + 3;
    if (i0 >= 12) i0 -= 12;
    if (i1 >= 12) i1 -= 12;
    if (i2 >= 12) i2 -= 12;
    if (i3 >= 12) i3 -= 12;
    int out = (GAUSS[0xff - frac] * v->brr[i0]) >> 11;
    out += (GAUSS[0x1ff - frac] * v->brr[i1]) >> 11;
    out += (GAUSS[0x100 + frac] * v->brr[i2]) >> 11;
    out = wrap16(out) + ((GAUSS[frac] * v->brr[i3]) >> 11);
    return (int16_t)(sat16(out) & ~1);
}

static void envelope_step(NDsp *d, NDspVoice *v) {
    int g = v->gain;
    int rate = 0;
    if (v->env_state == ENV_RELEASE) {
        rate = 31;
        g -= 8;
    } else if (!v->use_gain) {
        rate = v->rate[v->env_state];
        if (v->env_state == ENV_ATTACK) g += rate == 31 ? 1024 : 32;
        else g -= ((g - 1) >> 8) + 1;
    } else if (!v->direct_gain) {
        rate = v->rate[3];
        switch (v->gain_mode) {
        case 0: g -= 32; break;
        case 1: g -= ((g - 1) >> 8) + 1; break;
        case 2: g += 32; break;
        case 3: g += v->gain_preclamp < 0x600 ? 32 : 8; break;
        }
    } else {
        rate = 31;
        g = v->gain_direct;
    }
    int sustain = v->use_gain ? v->gain_sustain_level : v->sustain_level;
    if (v->env_state == ENV_DECAY && (g >> 8) == sustain) v->env_state = ENV_SUSTAIN;
    v->gain_preclamp = (uint16_t)(g & 0xffff);
    if (g < 0 || g > 0x7ff) {
        g = g < 0 ? 0 : 0x7ff;
        if (v->env_state == ENV_ATTACK) v->env_state = ENV_DECAY;
    }
    if (rate_hit(d, rate)) v->gain = (uint16_t)g;
}

static void voice_step(NDsp *d, int n) {
    NDspVoice *v = &d->v[n];
    int pitch = v->pitch;
    if (n > 0 && v->pitch_mod) pitch += ((d->v[n - 1].out >> 5) * pitch) >> 10;

    v->brr_header = d->ram[v->brr_addr];
    unsigned dir = (unsigned)d->dir_page + 4u * v->srcn;
    if (v->start_delay == 0) dir += 2; /* loop address once playing, start address while keying on */
    uint16_t sample_addr = (uint16_t)(d->ram[dir & 0xffff] | (d->ram[(dir + 1) & 0xffff] << 8));

    if (v->start_delay > 0) {
        if (v->start_delay == 5) {
            v->brr_addr = sample_addr;
            v->brr_nibble = 1;
            v->brr_pos = 0;
            v->brr_header = 0;
            d->regs[REG_ENDX] &= (uint8_t)~(1 << n);
        }
        v->gain = 0;
        v->start_delay--;
        v->pitch_counter = 0;
        if (v->start_delay > 0 && v->start_delay < 4) v->pitch_counter = 0x4000;
        pitch = 0;
    }

    int sample = 0;
    if (v->gain != 0) { /* a silent voice contributes nothing; the bookkeeping below still runs */
        sample = v->noise ? wrap16(d->noise * 2) : interpolate(v);
        sample = ((sample * v->gain) >> 11) & ~1;
    }

    if (d->reset || (v->brr_header & 0x03) == 1) { /* reset flag or end-without-loop block */
        v->env_state = ENV_RELEASE;
        v->gain = 0;
    }
    if (d->even) {
        if (v->key_off) v->env_state = ENV_RELEASE;
        if (v->key_on) {
            v->start_delay = 5;
            v->env_state = ENV_ATTACK;
            v->key_on = false;
        }
    }
    if (v->start_delay == 0) envelope_step(d, v);

    if (v->pitch_counter >= 0x4000) {
        brr_decode_group(d, v);
        if (v->brr_nibble >= 7) {
            if (v->brr_header & 1) {
                v->brr_addr = sample_addr;
                d->regs[REG_ENDX] |= (uint8_t)(1 << n);
            } else {
                v->brr_addr += 9;
            }
            v->brr_nibble = 1;
        } else {
            v->brr_nibble += 2;
        }
    }
    v->pitch_counter &= 0x3fff;
    v->pitch_counter = (uint16_t)(v->pitch_counter + pitch);
    if (v->pitch_counter > 0x7fff) v->pitch_counter = 0x7fff;

    d->regs[(n << 4) | REG_ENVX] = (uint8_t)(v->gain >> 4);
    d->regs[(n << 4) | REG_OUTX] = (uint8_t)(sample >> 8);
    v->out = (int16_t)sample;
    if (sample != 0) {
        int heard = (sample * d->voice_gain[n]) >> 8; /* host gain: audible and echoed, not pitch-modulating */
        d->mix_l = (int16_t)sat16(d->mix_l + ((heard * v->vol_l) >> 7));
        d->mix_r = (int16_t)sat16(d->mix_r + ((heard * v->vol_r) >> 7));
        if (v->echo) {
            d->echo_in_l = (int16_t)sat16(d->echo_in_l + ((heard * v->vol_l) >> 7));
            d->echo_in_r = (int16_t)sat16(d->echo_in_r + ((heard * v->vol_r) >> 7));
        }
    }
}

static void echo_step(NDsp *d) {
    d->fir_pos = (d->fir_pos + 1) & 7;
    uint16_t at = (uint16_t)(d->echo_base + d->echo_pos);
    d->fir_l[d->fir_pos] = (int16_t)(ram16(d, at) >> 1);
    d->fir_r[d->fir_pos] = (int16_t)(ram16(d, at + 2u) >> 1);

    int sum_l = 0, sum_r = 0;
    for (int i = 0; i < 8; i++) {
        sum_l += (d->fir_l[(d->fir_pos + i + 1) & 7] * d->fir[i]) >> 6;
        sum_r += (d->fir_r[(d->fir_pos + i + 1) & 7] * d->fir[i]) >> 6;
        if (i == 6) { sum_l = wrap16(sum_l); sum_r = wrap16(sum_r); }
    }
    sum_l = sat16(sum_l) & ~1;
    sum_r = sat16(sum_r) & ~1;

    d->mix_l = (int16_t)sat16(((d->mix_l * d->main_l) >> 7) + ((sum_l * d->echo_vol_l) >> 7));
    d->mix_r = (int16_t)sat16(((d->mix_r * d->main_r) >> 7) + ((sum_r * d->echo_vol_r) >> 7));

    int out_l = sat16(d->echo_in_l + wrap16((sum_l * d->feedback) >> 7)) & ~1;
    int out_r = sat16(d->echo_in_r + wrap16((sum_r * d->feedback) >> 7)) & ~1;
    if (d->echo_writes && !d->echo_write_locked) {
        d->ram[at] = out_l & 0xff;
        d->ram[(at + 1) & 0xffff] = (uint8_t)(out_l >> 8);
        d->ram[(at + 2) & 0xffff] = out_r & 0xff;
        d->ram[(at + 3) & 0xffff] = (uint8_t)(out_r >> 8);
    }
    if (d->echo_pos == 0) d->echo_len = (uint16_t)(d->echo_delay * 4);
    d->echo_pos = (uint16_t)(d->echo_pos + 4);
    if (d->echo_pos >= d->echo_len) d->echo_pos = 0;
}

void ndsp_cycle(NDsp *d) {
    d->mix_l = d->mix_r = d->echo_in_l = d->echo_in_r = 0;
    for (int i = 0; i < 8; i++) voice_step(d, i);
    echo_step(d);
    d->counter = d->counter == 0 ? 30720 : d->counter - 1;
    if (rate_hit(d, d->noise_rate)) {
        int bit = (d->noise & 1) ^ ((d->noise >> 1) & 1);
        d->noise = (int16_t)(((d->noise >> 1) & 0x3fff) | (bit << 14));
    }
    d->even = !d->even;
    if (d->mute) d->mix_l = d->mix_r = 0;
    d->ring[(d->ring_pos & (NDSP_RING - 1)) * 2] = d->mix_l;
    d->ring[(d->ring_pos++ & (NDSP_RING - 1)) * 2 + 1] = d->mix_r;
}

uint8_t ndsp_read(const NDsp *d, uint8_t reg) { return d->regs[reg & 0x7f]; }

void ndsp_write(NDsp *d, uint8_t reg, uint8_t val) {
    reg &= 0x7f;
    NDspVoice *v = &d->v[reg >> 4];
    switch (reg) {
    case 0x00: case 0x10: case 0x20: case 0x30: case 0x40: case 0x50: case 0x60: case 0x70: v->vol_l = (int8_t)val; break;
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x41: case 0x51: case 0x61: case 0x71: v->vol_r = (int8_t)val; break;
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52: case 0x62: case 0x72:
        v->pitch = (uint16_t)((v->pitch & 0x3f00) | val); break;
    case 0x03: case 0x13: case 0x23: case 0x33: case 0x43: case 0x53: case 0x63: case 0x73:
        v->pitch = (uint16_t)(((v->pitch & 0x00ff) | (val << 8)) & 0x3fff); break;
    case 0x04: case 0x14: case 0x24: case 0x34: case 0x44: case 0x54: case 0x64: case 0x74: v->srcn = val; break;
    case 0x05: case 0x15: case 0x25: case 0x35: case 0x45: case 0x55: case 0x65: case 0x75:
        v->rate[0] = (uint8_t)((val & 0x0f) * 2 + 1);
        v->rate[1] = (uint8_t)(((val & 0x70) >> 4) * 2 + 16);
        v->use_gain = (val & 0x80) == 0;
        break;
    case 0x06: case 0x16: case 0x26: case 0x36: case 0x46: case 0x56: case 0x66: case 0x76:
        v->rate[2] = val & 0x1f;
        v->sustain_level = (val & 0xe0) >> 5;
        break;
    case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77:
        v->direct_gain = (val & 0x80) == 0;
        v->gain_mode = (val & 0x60) >> 5;
        v->rate[3] = val & 0x1f;
        v->gain_direct = (uint16_t)((val & 0x7f) * 16);
        v->gain_sustain_level = (val & 0xe0) >> 5;
        break;
    case 0x0c: d->main_l = (int8_t)val; break;
    case 0x1c: d->main_r = (int8_t)val; break;
    case 0x2c: d->echo_vol_l = (int8_t)val; break;
    case 0x3c: d->echo_vol_r = (int8_t)val; break;
    case 0x4c: for (int i = 0; i < 8; i++) d->v[i].key_on = (val >> i) & 1; break;
    case 0x5c: for (int i = 0; i < 8; i++) d->v[i].key_off = (val >> i) & 1; break;
    case 0x6c:
        d->reset = (val & 0x80) != 0;
        d->mute = (val & 0x40) != 0;
        d->echo_writes = (val & 0x20) == 0;
        d->noise_rate = val & 0x1f;
        break;
    case REG_ENDX: val = 0; break; /* any write clears ENDX */
    case 0x0d: d->feedback = (int8_t)val; break;
    case 0x2d: for (int i = 0; i < 8; i++) d->v[i].pitch_mod = (val >> i) & 1; break;
    case 0x3d: for (int i = 0; i < 8; i++) d->v[i].noise = (val >> i) & 1; break;
    case 0x4d: for (int i = 0; i < 8; i++) d->v[i].echo = (val >> i) & 1; break;
    case 0x5d: d->dir_page = (uint16_t)(val << 8); break;
    case 0x6d: d->echo_base = (uint16_t)(val << 8); break;
    case 0x7d: d->echo_delay = (uint16_t)((val & 0x0f) * 512); break;
    case 0x0f: case 0x1f: case 0x2f: case 0x3f: case 0x4f: case 0x5f: case 0x6f: case 0x7f: d->fir[reg >> 4] = (int8_t)val; break;
    }
    d->regs[reg] = val;
}

void ndsp_get_samples(const NDsp *d, int16_t *out, int n) {
    const float frame = 534.0f; /* NTSC samples per video frame */
    double step = frame / n;
    double at = d->ring_pos - frame;
    for (int i = 0; i < n; i++) {
        int k = ((int)at) & (NDSP_RING - 1);
        out[i * 2] = d->ring[k * 2];
        out[i * 2 + 1] = d->ring[k * 2 + 1];
        at += step;
    }
}
