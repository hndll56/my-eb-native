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
