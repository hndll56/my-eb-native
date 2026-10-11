# EB-Native status

Honest status of the PC native port (`port/unix`). Legend: **Done** = implemented and verified by the method listed; **Partial** = works with known limits; **Not done** = absent.

## Build

```
cd port/unix
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs SDL2, a C compiler, CMake, Python 3 with `ebtools` (`uv pip install -e .` in the repo root, or pass `-DEBTOOLS_EXE=/path/to/ebtools`; `-DCMAKE_PREFIX_PATH=...` for a non-system SDL2), and assets extracted from your own donor ROM (`earthbound.sfc`, see README). Vulkan is optional: if the loader or a device is missing the port falls back to the SDL renderer. To rebuild SPIR-V headers after editing a shader: `port/unix/shaders/compile.sh` (needs glslang).

## Status

| Area | Status | Notes |
|---|---|---|
| Game logic | Done | Native C (`src/`), no 65816 CPU emulation. |
| Vulkan presentation | Done | `platform/vk_renderer.c`: integer/pixel-art scaling, overlays. |
| Vulkan PPU compositing | Partial | `shaders/ppu.comp` composes BG/OBJ/windows/color math/mosaic/HDMA distortion on the GPU. Only the native 256x224 BGR565 viewport; anything else falls back to the CPU compositor. The frame is read back to host memory (~114 KB memcpy plus a fence wait) because overlays, screenshots and the CPU-side present path need the pixels. A zero-copy GPU-to-texture path was deliberately not added: the gain can't be measured without a real GPU, so GPU compositing is not claimed to be faster. Setting: "PPU mode". |
| GPU/CPU equivalence | Verified on tested scenes | `--gpu-compare`: 0 differing pixels over >4000 frames of intro/title/start scene and ~600 frames each in battle groups 20, 150, 300. Tested on SwiftShader only (no real GPU available). Not every battle background/effect was covered. |
| Audio | Partial (SPC700 CPU + DSP still emulated) | Music/SFX work through the SPC700/DSP driver **emulated by lakesnes**. This is the one remaining emulated component. Separate master/music/SFX volume. Output was captured with SDL's disk audio driver: music and SFX produce sound, master volume 50% halves the peak, music=0 and sfx=0 each silence only their own part. Not listened to on real speakers. |
| Settings menu | Done (checked via Xvfb + SwiftShader with real key events, GPU PPU active; layout bugs found and fixed) | Resolution, window mode, VSync, scaling, volumes, keyboard + gamepad remap, PPU mode; persisted in config with safe defaults. |
| Debug menu | Done | Off by default (F5). FPS/frame time, position, flags, warp, battle, party/items, logs. `--debug-script` for automation. |
| Save/load | Done for the tested cases | In-game save is the original format (3 slots, each written twice with ADD+XOR checksums; compatible with real-hardware SRAM layout). A bad primary copy falls back to the backup; two bad copies are rejected, never loaded silently. Writes are flushed/fsynced and failures are logged. Verified by `--selftest-savestate`. F6/F7 snapshot is same-binary only. A save written by the game's own new-game flow was checksum-verified, and booting from it shows the slot as occupied and continues. Saving later from the phone/menu in the overworld was not exercised. |
| Stability | Checked | ASAN+UBSAN build ran ~15000 frames of intro and battles headless: no reports. Clean from-scratch Release build passes (upstream code has 21 compiler warnings, none in the new files). |
| Software PPU | Kept | Still the reference and fallback path (`src/snes/ppu_render.c`). |
| Gamepad | Partial | `--selftest-pad` drives an in-process virtual SDL gamepad: buttons, left stick + dead zone, remapping, hot-unplug all verified. A physical controller has not been tried. |
| Asset overrides (modding) | Done for whole-asset replacement | `--mods DIR`, validated, original fallback; see `docs/modding-assets.md`. Verified by `--selftest-mods` and a visual before/after. |
| Automated tests | Done | `ctest --test-dir build` runs savestate+save, gamepad, override rules and `--help`. |
| Windows/macOS builds | Not tested here | |

## Fixed in this work

- Null `enemy_config_table` crash when starting a battle (`game_init` now calls `load_enemy_spawn_data`).
- Debug command/script limits that silently dropped commands.

## Test commands

```
earthbound --gpu-compare --gpu-from N      # compare GPU frame to CPU frame, log differences
earthbound --debug --debug-script FILE     # FRAME:CMD lines: warp, tele, heal, money, item, battle, flag, pad HEX, shot FILE
earthbound --ppu gpu|cpu                    # force GPU / CPU compositing
earthbound --selftest-savestate             # F6/F7 snapshot tests + in-game save tests (corrupt primary -> backup, both corrupt -> rejected)
```

## Attribution and licenses

Game data comes from the user's own ROM and is not distributed. Vendored: `src/vendor/lakesnes` (see its `LICENSE.txt`), `src/vendor/tamp` (see its `LICENSE`). Upstream decompilation: BrianPugh/earthbound. All license files are preserved unchanged.


## QoL merge (2026-10-11)
The newer QoL build was merged selectively; see `PROJECT_PROGRESS.md` and `IMPLEMENTATION_CHECKLIST.md` for status of every item.
New in this pass: settings for fast-forward speed, audio DSP (restart), HQ music and "restore all defaults"; binding conflicts swap; config validation repairs duplicate bindings.
Optional music: `<mods>/music/NNN.wav` (NNN = 3-digit track id, 16-bit PCM mono/stereo 8-192 kHz, <=10 min, optional `NNN.loop` = loop start frame). Invalid files fall back to the original music. Distribute only recordings you have rights to.
Tests (`ctest`): savestate_and_save, gamepad_virtual, cli_help, asset_override_rules, video_layout, config_handling, hq_music_validation, audio_native_dsp.
