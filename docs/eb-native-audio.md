# Native audio status
* SPC700 CPU + N-SPC driver: still emulated by lakesnes (`src/vendor/lakesnes`), required.
* S-DSP: native in `src/audio/ndsp.c`, bit-exact against lakesnes on 191 tracks x 600 frames and 24 tracks x 3600 frames (`--selftest-audio`, `audio_native_dsp` in CTest uses a short run). Default is the original DSP; choose Native in Settings (applies at next start) or `--audio-dsp native|shadow|lakesnes`.
* Remaining work to drop lakesnes: port the SPC700 driver, using DSP register-write equivalence as the oracle.
* Optional replacement recordings: see `docs/eb-native-status.md` (HQ music).
