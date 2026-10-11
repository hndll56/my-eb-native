# Display modes (Classic / Modern) and Save anywhere

## Display modes

The game library renders a frame of `EB_VIEWPORT_WIDTH x EB_VIEWPORT_HEIGHT` pixels. The desktop
port (`port/unix/CMakeLists.txt`) defaults to **400x224**; the SNES picture is the centred 256x224
area (`EB_VIEWPORT_PAD_LEFT`), and the map layers and sprites fill the extra width, so the extra
columns are real game world, not a stretch.

`display` in `settings.ini` / **Settings > Display mode** chooses what is shown:

| Mode | Shows | Pixel shape |
|---|---|---|
| Classic | the centred 256x224 area of the frame | follows *Aspect ratio* (8:7 native, 4:3 CRT, 16:9 stretched) |
| Modern | the whole frame (400x224 by default, about 16:9) | always square |

Switching is presentation-only (`video_source_rect()` in `platform/video_layout.c`), takes effect
immediately and does not touch game state, so savestates and saves are unaffected by the toggle.
The SDL presenter crops with a source rectangle; the Vulkan presenter draws the whole texture with a
viewport sized so the visible part lands on the scissor rectangle (no shader change).
Menus and the FPS counter are laid out inside the visible area.

Defaults keep existing behaviour: a build with a wide frame defaults to Modern, a 256x224 build to
Classic. A 256x224 build has nothing wider to show, so the row reads "Classic only (N/A)".

### Using Modern mode
Build with a wide frame (the port default). If your build directory was configured earlier with 256,
the cached value wins: delete `port/unix/build` or pass `-DEB_VIEWPORT_WIDTH=400`.

### Limitations
* The frame width is a compile-time constant (static buffers are sized from it). Other window shapes
  than the frame's own (16:10, 21:9, 4:3) get bars, they do not get more world. A truly adaptive
  width would need runtime-sized buffers across the engine.
* With a wide frame the engine also spawns and culls entities across the wider area, even in
  Classic mode (it only crops what is shown).
* When the frame is wider than 256 the Vulkan GPU compositor is not used (CPU compositor instead;
  see `eb-native-status.md`).

## Save anywhere

The savestate (crash-safe ping-pong slot files, `src/core/state_dump.c`,
`platform/sdl2_savestate.c`) already existed on F6/F7. It is now also reachable from
**Esc > Settings > SAVE ANYWHERE** and reports the result on screen ("STATE SAVED",
"SAVE FAILED - SEE LOG", ...). The request runs at the game loop's safe point; if the game is
waiting at an input prompt that never unwinds, the request fails and is reported, nothing is written
half-way. The original 3-slot in-game save is untouched.
Savestates are same-binary only: a state from a build with a different frame size should not be
expected to load.

## Tests
`port/unix/tests/test_video_layout.c` covers the pure viewport helpers (crop rectangles, integer
scaling, aspect handling at 16:9 / 16:10 / 21:9 / 4:3, degenerate sizes):
`cmake -S port/unix -B build -DEB_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build`.

## Modern mode fills the window (update)

In Modern mode the visible part of the 400x224 frame now follows the window aspect
(`video_modern_view`): full height, as wide as needed, centred. With Scaling = Fit the picture covers
the window with no bars at 16:9, 16:10, 4:3 and 5:4 (square pixels, no stretch; at most one source
pixel of rounding is absorbed). Windows wider than 400:224 (e.g. 21:9) keep side bars because the
frame is only rendered 400 wide. Scaling = Integer keeps pixel-exact multiples and therefore bars.
New default scaling for Modern is Fit; an existing earthbound.ini keeps its saved scale_mode.
Verified: unit tests (video_layout, 8/8 ctest). NOT verified: visual output on real Windows/GPU.
