# IMPLEMENTATION_CHECKLIST

| # | Item | Status | Evidence / notes |
|---|---|---|---|
| 1 | Save Anywhere (menu, F6/F7, toast, atomic ping-pong files) | BUILD VERIFIED; savestate round trip TEST VERIFIED (`savestate_and_save`) | Menu action path not exercised end-to-end in-game by me; same-binary states only |
| 2 | Classic 4:3 / Modern widescreen | TEST VERIFIED (viewport maths, `video_layout`); RUNTIME seen (menu, frames 400x224) | No claim of extra visible world in interiors; wide frame is compile-time |
| 3 | Optional high-quality music | RUNTIME VERIFIED | `music/NNN.wav` from `--mods`, toggle in settings, validation unit tests (`hq_music_validation`), disk-audio capture: pure 1 kHz when on, original music when off. 32 kHz output only; MSU-1 not done; no music shipped |
| 4 | Persistent config menu | TEST VERIFIED (`config_handling`: round trip, malformed, out-of-range, duplicate binds, defaults); RUNTIME VERIFIED (Options screens, ini written) | Restore all defaults row added |
| 5 | Input rebinding / conflicts / reset | TEST VERIFIED (swap on conflict, `config_handling`, `gamepad_virtual`) | Conflict = swap, status text shown |
| 6 | Configurable fast-forward | BUILD + RUNTIME VERIFIED (setting persists: ff_speed=5 in ini) | 2x-8x, hold Tab; actual speed-up not benchmarked |
| 7 | Update checker | BLOCKED | Needs a release endpoint + signing scheme; cannot be tested here (no network). A control without a working backend was deliberately not added |
| 8 | Startup/data prep/shutdown | PARTIAL | Assets embedded at build time; no ROM bundled; bad config/save paths handled with messages; `--mods` missing dir only warns |
| 9 | Native audio DSP | TEST VERIFIED | `audio_native_dsp` (bit-exact vs lakesnes), selectable in settings (restart) and `--audio-dsp` |
| 10 | Main menu | RUNTIME VERIFIED | Continue greyed without save, Exit exits, New Game/Continue enter game, Options persists |
