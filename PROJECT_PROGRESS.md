# PROJECT_PROGRESS

Updated 2026-10-11. Status vocabulary: NOT STARTED / IN PROGRESS / IMPLEMENTED / BUILD VERIFIED / TEST VERIFIED / RUNTIME VERIFIED / BLOCKED.

## Project identification (by content, not name)
* **Old working project** = this tree before the merge: native audio DSP (`src/audio/ndsp.*`), asset override/modding (`src/data/asset_override.*`), CTest wiring, gamepad/save self-tests. Backup: `work/backups/main-before-qol-merge-20261011-0039.tar.gz`.
* **Newer build** = `c04c30fb-earthbound-main-qol-patched.zip`: a descendant of an earlier snapshot of the old project that adds main menu, save toast, display modes (400-wide frame), settings rows, video layout tests. It lacks all of the old project's audio/modding/test work.
* **Baseline** = the old project; the newer build was merged in selectively (files the old project had not touched were taken as-is, 5 overlapping files merged by hand). Nothing was replaced wholesale; no destructive git commands (the tree is not a git repo).

## Migration inventory
| Change | Old impl | New impl | Strategy | Dependencies | Verification |
|---|---|---|---|---|---|
| Main menu (New Game/Continue/Options/Exit) | none | `platform/main_menu.c` | taken as-is, uses existing settings menu | settings_menu, video overlay | RUNTIME (Xvfb screenshots + key events) |
| Save anywhere (menu + toast) | F6/F7 only | `save_toast.c`, settings rows | taken as-is | state_dump | build + existing savestate test; in-menu path runtime-seen only |
| Classic/Modern display, 400x224 frame | 256x224 | `video_layout.c`, wide default | taken; default width 400 | EB_VIEWPORT_WIDTH | unit test video_layout; screenshots |
| Settings menu | basic | more rows | taken, then extended (below) | config | runtime |
| FPS overlay in wide frame | fixed | `eb_fps_overlay_right` | taken | game_main | build |
| Windows fsync/verify fixes | n/a | partial | adopted, `sdl2_save.c` fixed (QoL include was dead code inside `!_WIN32`) | | build (Linux only) |
| Audio DSP native / hooks | yes | absent | kept | | `--selftest-audio` |
| Asset overrides, `--mods` | yes | absent | kept | | `--selftest-mods` |
| CTest tests | 4 | `EB_BUILD_TESTS` option | merged (always enabled) | | ctest |

## QoL features
See `IMPLEMENTATION_CHECKLIST.md` for per-item status.

## Known limitations (honest)
* Default build is the 400-wide frame; the Vulkan GPU compositor only supports 256x224, so wide builds use the CPU compositor. `-DEB_VIEWPORT_WIDTH=256` restores the GPU path (0/600 frames differed in `--gpu-compare`).
* In small interior rooms the extra width is mostly black and the room is not centred in the wide frame (engine behaviour inherited from the newer build; observed in a screenshot, not changed).
* Continue starts the normal boot (logo, title, file select); it does not jump straight into the save.
* SPC700 CPU is still emulated (lakesnes); only the DSP is native.
* Windows/macOS were not built or run.
