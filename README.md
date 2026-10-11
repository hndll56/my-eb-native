<div align="center">

# EarthBound Native

**An experimental native-PC port and engine reimplementation inspired by EarthBound / Mother 2.**

Building toward a stable, standalone game experience—with a focus on learning, portability, and a clean foundation.

<br>

![Status](https://img.shields.io/badge/status-in%20development-orange?style=for-the-badge)
![Language](https://img.shields.io/badge/language-C%20%2F%20C%2B%2B-blue?style=for-the-badge)
![Platform](https://img.shields.io/badge/target-PC-lightgrey?style=for-the-badge)

[Repository](https://github.com/hndll56/my-eb-native) · [Report a bug](https://github.com/hndll56/my-eb-native/issues) · [Licensing & provenance](LICENSING.md)

</div>

---

## ✨ About the Project

**EarthBound Native** is an experimental effort to bring an EarthBound-inspired game runtime to modern platforms. This repository builds on an existing EarthBound source-recreation project and is being developed incrementally, with engine stability taking priority over adding gameplay features too early.

The long-term goal is to establish a reliable native foundation for the game: a consistent startup flow, usable menus and settings, dependable input handling, and—eventually—a working gameplay loop.

> **Important:** This project is still a work in progress. The current engine shell can start and display the menu, options, and controls screens, but **the game is not playable yet**. Starting a new game is intentionally disabled while core functionality is being developed.

## 🚧 Current Status

| Component | Status |
|---|---|
| Application startup / engine shell | Working |
| Main menu | Available; being refined |
| Options and controls screens | Available |
| New Game / gameplay | Not implemented yet |
| Full playthrough | Not playable yet |
| Stability and testing | Active development |

These statuses describe the current state of this repository, not every capability of the upstream project it derives from.

## 🎯 Development Priorities

Development is focused on getting the fundamentals right before expanding scope:

1. **Stability first** — fix startup, crashes, resource handling, and other core issues.
2. **Reliable game foundation** — make the engine and game-state flow predictable and testable.
3. **Menus and configuration** — refine the main menu, options, controls, and feedback.
4. **Rendering quality** — improve pixel-art presentation with carefully tuned scaling and optional visual filters, without making the image blurry.
5. **Debugging tools** — expose useful diagnostics and debug options for development.
6. **Gameplay integration** — enable New Game only when the required game systems are ready.

## 🖥️ Platform Goals

The immediate focus is a native desktop build. Additional platforms, including Android, may be explored as the engine becomes more stable. Platform support should be considered **experimental until it has been built and tested on the target device**.

| Platform | Status |
|---|---|
| Windows / desktop | Active development |
| Linux / other desktop systems | Not yet confirmed for this project |
| Android | Experimental / not yet confirmed |

## ▶️ How to Use

> **Current limitation:** the repository's native engine shell can open the main menu, options, and controls screens, but the game is **not playable yet**. The steps below explain how to prepare, build, and launch the current desktop version; they do not unlock gameplay.

### 1. Get the source

Install [Git](https://git-scm.com/), then open a terminal:

```bash
git clone https://github.com/hndll56/my-eb-native.git
cd my-eb-native
git submodule update --init --recursive
```

### 2. Install the build tools

The current `make unix` workflow is designed for a Unix-like shell.

**Ubuntu / Debian (including WSL2):**

```bash
sudo apt update
sudo apt install git make cmake build-essential pkg-config libsdl2-dev python3 python3-venv python3-pip
```

**Fedora:**

```bash
sudo dnf install git make cmake gcc gcc-c++ pkgconf-pkg-config SDL2-devel python3
```

**macOS (with Homebrew):**

```bash
brew install git make cmake sdl2 pkg-config python
```

Use Python 3.10 or newer. On Windows, use **WSL2 with Ubuntu** for the documented build workflow; a native Windows build is not currently documented or verified here.

### 3. Provide your own game ROM

The build scripts extract game assets from a compatible US EarthBound ROM. Obtain the ROM independently and only if you are legally entitled to use it. Place it in the repository root and name it:

```text
earthbound.sfc
```

The ROM is not included in this repository. **Do not commit or upload the ROM or extracted copyrighted assets.**

### 4. Build the desktop version

From the repository root, run:

```bash
make unix
```

The Makefile checks dependencies, prepares the Python environment, extracts assets if needed, configures CMake, and compiles the desktop port. The first build can take a while.

If the command fails, read the first error reported by the build and verify that all prerequisites above are installed.

### 5. Launch the application

After a successful build, run:

```bash
./port/unix/build/earthbound
```

The current shell is for development and testing. You can explore the screens that are implemented, but **New Game/gameplay is not available yet**.

### 6. Run automated checks (optional)

After building, run the available tests with:

```bash
ctest --test-dir port/unix/build --output-on-failure
```

This checks several engine/platform helpers and command-line self-tests; passing these tests does not mean the full game is playable.

### Troubleshooting

- **ROM not found:** confirm `earthbound.sfc` is in the repository root, next to the top-level `Makefile`.
- **SDL2 development files missing:** install the SDL2 development package for your operating system.
- **`make` or Python environment errors on Windows:** use the documented WSL2/Ubuntu workflow; the Unix Makefile is not a native PowerShell script.
- **Build succeeds but the game is incomplete:** this is expected at the current development stage. Check [Current Status](#-current-status) for what is and is not implemented.

---

## 🛠️ Building and Testing

Build instructions may change while the project structure is being stabilized. Start by reviewing the repository's build files and any platform-specific documentation before compiling.

When reporting a build or runtime issue, please include:

- Your operating system and architecture
- The exact build command you ran
- The complete error output or crash log
- Steps to reproduce the issue
- Whether the issue happens on a clean build

**Please do not upload ROMs, copyrighted game assets, save files, or other proprietary game data to issues or pull requests.**

## 🧭 Project Background and Source Provenance

This repository is derived from an existing EarthBound source-recreation effort. The known project lineage includes:

- [Herringway/ebsrc](https://github.com/Herringway/ebsrc)
- [BrianPugh/earthbound](https://github.com/BrianPugh/earthbound)

See [LICENSING.md](LICENSING.md) for the current licensing and source-provenance notes. The license status of inherited code has not been fully verified, so this repository does **not** claim that the entire codebase is covered by a blanket MIT license.

EarthBound / Mother 2 and its original game assets are the property of their respective rights holders. This is an independent, unofficial project and is not affiliated with or endorsed by Nintendo or the original rights holders.

## 🤝 Contributing

Contributions, bug reports, and technical discussion are welcome. At this stage, the most useful contributions help make the existing foundation more reliable.

- Reproduce and document bugs
- Improve build reliability and portability
- Add focused tests for core systems
- Improve menus, input handling, and diagnostics
- Keep changes small and explain how they were tested

Please check the current project state before proposing major features. Gameplay and platform support should not be described as complete unless they have been implemented and verified.

## 📜 License

The licensing status of inherited source code is **under review**. Please read [LICENSING.md](LICENSING.md) before redistributing or relicensing code from this repository. Do not assume all files have the same license.

---

<div align="center">

**A work in progress. Stability first, gameplay next.**

<sub>Unofficial community project · Built for experimentation and learning</sub>

</div>
