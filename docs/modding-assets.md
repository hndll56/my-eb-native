# External asset overrides (desktop port)

Run with `earthbound --mods DIR`. For every embedded asset the loader looks for `DIR/<asset path>`. The asset path is the same relative path used by the build (the comment next to each name in the generated `asset_ids.h`), e.g. `DIR/US/intro/title_screen.pal.lzhal`, `DIR/battle_bgs/palettes/001.pal`, `DIR/audiopacks/3.ebm`.

Rules, enforced at startup (a rejected file is reported on stderr and the original asset is used):

| Asset kind | Accepted when |
|---|---|
| `*.lzhal` (compressed graphics, palettes, arrangements) | decompresses cleanly (max 1 MiB output); any size. Create with `ebtools`' `hallz.compress`. |
| `*.ebm` (audio packs) | non-empty, at most 64 KiB (SPC RAM). The content is not otherwise checked. |
| everything else (`*.bin` tables, raw data) | exactly the original size (fixed-layout tables) |
| any | non-empty, at most 4 MiB, readable |

Limits: overrides replace whole assets, not parts of them; there is no hot reload (restart the game); nothing checks that a replacement *looks* right or that a pack is valid music. Only assets the game reads through `ASSET_DATA/ASSET_SIZE` can be overridden (all embedded assets today). Text and map data are ordinary assets too and follow the same rules, but their formats are the game's own.

Verified: `earthbound --selftest-mods` (validation rules) and a visual check that an inverted `US/intro/title_screen.pal.lzhal` changes the title screen while every other captured frame stays byte-identical.
