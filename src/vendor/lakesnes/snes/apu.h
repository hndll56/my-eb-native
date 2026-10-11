
#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Apu Apu;

#ifdef EB_ENABLE_VERIFY
#include "snes.h"
#else
typedef struct Snes Snes;
#endif
#include "spc.h"
#include "dsp.h"
#include "statehandler.h"

typedef struct Timer {
  uint8_t cycles;
  uint8_t divider;
  uint8_t target;
  uint8_t counter;
  bool enabled;
} Timer;

/* EB-Native: optional DSP hooks. With `replace` false the handler is called in addition to the
 * built-in DSP (shadow mode: used to compare an alternative DSP, called BEFORE the built-in one
 * so it sees the same RAM); with `replace` true the built-in DSP is bypassed completely. */
typedef struct ApuDspHooks {
  void* ctx;
  void (*cycle)(void* ctx);
  void (*write)(void* ctx, uint8_t reg, uint8_t val);
  uint8_t (*read)(void* ctx, uint8_t reg);
  bool replace;
} ApuDspHooks;

struct Apu {
  Snes* snes;
  const ApuDspHooks* dspHooks;
  bool palTiming;
  Spc* spc;
  Dsp* dsp;
  uint8_t ram[0x10000];
  bool romReadable;
  uint8_t dspAdr;
  uint32_t cycles;
  uint8_t inPorts[6]; // includes 2 bytes of ram
  uint8_t outPorts[4];
  Timer timer[3];
};

Apu* apu_init(Snes* snes);
void apu_free(Apu* apu);
void apu_reset(Apu* apu);
void apu_handleState(Apu* apu, StateHandler* sh);
int apu_runCycles(Apu* apu, int wantedCycles);
uint8_t apu_read(Apu* apu, uint16_t adr);
void apu_write(Apu* apu, uint16_t adr, uint8_t val);
uint8_t apu_spcRead(void* mem, uint16_t adr);
void apu_spcWrite(void* mem, uint16_t adr, uint8_t val);
void apu_spcIdle(void* mem, bool waiting);

#endif
