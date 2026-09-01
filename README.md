# KF2 Optimizer Next

[![Windows CI](https://github.com/zeroking1998/KF2-Optimizer-Next/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/zeroking1998/KF2-Optimizer-Next/actions/workflows/windows-ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg)](#build-from-source)

KF2 Optimizer Next is a free, portable Windows companion for Killing Floor 2.
It watches game performance and safely adjusts supported graphics, LOD, corpse
physics, effects, and optional FleX quality to help keep FPS stable.

> This is an unofficial community project and is not affiliated with or
> endorsed by Tripwire Interactive. The project is alpha software.

## Download

1. Open [GitHub Releases](https://github.com/zeroking1998/KF2-Optimizer-Next/releases).
2. Download the portable Windows x64 ZIP.
3. Extract the complete `KF2OptimizerNext` folder.
4. Run `KF2Optimizer.exe`.

No installer, Windows service, scheduled task, or account is required. Keep the
executable beside its `Data` folder. The app is currently unsigned, so Windows
SmartScreen may ask you to confirm the first launch.

The official ZIP contains every project-owned runtime file required by that
version. Players do not need Visual Studio, CMake, the KF2 SDK, or a separate
telemetry download.

## What it does

- Measures live, average, and 1% low FPS, frame time, CPU, GPU, RAM, and VRAM.
- Uses a target from 30 to 240 FPS and adapts only when verified pressure exists.
- Adjusts supported graphics, LOD, effects, corpse load, physics, and collision.
- Leaves FleX off unless the user explicitly enables it in KF2.
- Provides a separate overlay, graphics controls, backups, updates, and repair.
- Confirms runtime changes with a matching receipt or exact readback.
- Restores protected session changes when KF2 closes or after recovery.

It does not change damage, health, weapons, enemy AI, spawning, networking, or
competitive gameplay.

## Quick start

1. Start KF2 Optimizer.
2. Confirm the detected Killing Floor 2 folder.
3. Choose **Target FPS** and **Maximum corpses** on Home.
4. Leave **Adaptive optimization** on, or switch it off if you only want the
   fixed goals, telemetry, overlay, and your own graphics settings.
5. Start or continue KF2. Steam and desktop-shortcut launches are supported.

The [User Guide](docs/USER_GUIDE.md) explains every screen and recovery option.

## Build from source

The normal contributor build needs Windows 10 or newer, Git, PowerShell 7,
CMake 3.28 or newer, and Visual Studio 2022 C++ Build Tools.

The easiest Windows setup is:

1. Download the source ZIP and extract it.
2. Double-click `setup.cmd` to install or confirm the public development tools.
3. Open a new terminal and run `build.cmd` for a tested developer build.
4. Install the official KF2 SDK through Steam and run `package.cmd` for the
   complete portable application.

If the tools are already installed, use a terminal:

```powershell
git clone https://github.com/zeroking1998/KF2-Optimizer-Next.git
cd KF2-Optimizer-Next
pwsh -NoProfile -File ./tools/check_requirements.ps1
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Release
```

That last command builds the app and runs all tests. The executable is written
to `out/build/windows-x64-release/Release/KF2Optimizer.exe`.
After cloning, Windows users can also double-click `build.cmd`; it checks the
PC, builds Release, runs the tests, and prints the output path.

Every GitHub pull request also creates a temporary Windows x64 developer
artifact on its **Actions** run. It is useful for reviewing a change but is not
an official complete release package.

A complete portable package also needs the official KF2 SDK installed through
Steam because the protected telemetry module must be compiled locally. Follow
the short [Build Guide](docs/BUILDING.md) for the exact package command,
automatic verified seed download, Visual Studio setup, and common errors.

## Project structure

| Path | Purpose |
|---|---|
| `src` and `include/kf2` | Native C++ application code |
| `assets/offline_telemetry` | Project-authored KF2 telemetry source |
| `tests` | Unit, Windows, integration, and contract tests |
| `tools` | Build, test, package, and validation commands |
| `docs` | User, contributor, architecture, and safety documentation |
| `.github` | CI, issue forms, and pull-request guidance |

## Start here

| Goal | Document |
|---|---|
| Use the app | [User Guide](docs/USER_GUIDE.md) |
| Build or test it | [Build Guide](docs/BUILDING.md) |
| Understand how it works | [How It Works](docs/HOW_IT_WORKS.md) |
| Change the code | [Developer Guide](docs/DEVELOPER_GUIDE.md) |
| Contribute | [Contributing](CONTRIBUTING.md) |
| Get help | [Support](SUPPORT.md) |
| Browse all documents | [Documentation Index](docs/README.md) |

## Contributing

Bug reports, documentation fixes, tests, and focused pull requests are welcome.
You do not need to understand the entire codebase. Start with
[CONTRIBUTING.md](CONTRIBUTING.md), run the requirement check, and let the same
Debug and Release tests used by GitHub CI verify your work.

All public text, UI text, code comments, logs, tests, and documentation use
clear English.

## License

KF2 Optimizer Next is licensed under
[GNU GPL version 3.0 only](LICENSE) (`GPL-3.0-only`). Third-party components
retain their own licenses; see [Third-Party Notices](THIRD_PARTY_NOTICES.md).
The repository does not contain the KF2 game, SDK, original game assets, or the
original NVIDIA FleX runtime.
