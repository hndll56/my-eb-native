#ifndef EB_GPU_PPU_H
#define EB_GPU_PPU_H

#include "core/types.h"
#include <stdbool.h>

/* Compose the current PPU state into `out` (EB_VIEWPORT_WIDTH x EB_VIEWPORT_HEIGHT) on the GPU.
 * Returns false when the GPU path cannot be used (no Vulkan compute, non-native viewport); the
 * caller then falls back to the CPU compositor (ppu_render_frame). */
bool gpu_ppu_render(pixel_t *out);

/* Debug aid: render the same state with the CPU compositor and report differing pixels. */
void gpu_ppu_set_compare(bool on);
/* Test aid: use the CPU compositor for the first `frame` frames (fast-forward to a scene). */
void gpu_ppu_set_start_frame(int frame);

#endif
