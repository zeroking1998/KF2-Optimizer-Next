# KF2 Optimizer Next

[![Windows CI](https://github.com/zeroking1998/KF2-Optimizer-Next/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/zeroking1998/KF2-Optimizer-Next/actions/workflows/windows-ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg)](#build-from-source)

KF2 Optimizer Next is a free, portable Windows companion for Killing Floor 2.
It measures frame times and adjusts graphics, effects and corpse physics when
Adaptive detects sustained pressure. If you enable NVIDIA FleX in KF2, the
Optimizer can run it with an optimized runtime for that session.

> This is an unofficial community project and is not affiliated with or
> endorsed by Tripwire Interactive. The project is alpha software.

## Download

1. Download the [0.0.5-alpha Windows x64 ZIP](https://github.com/zeroking1998/KF2-Optimizer-Next/releases/download/v0.0.5-alpha/KF2OptimizerNext-v0.0.5-alpha-win64.zip).
2. Extract the entire ZIP to a folder you can write to.
3. Run `KF2Optimizer.exe` from that folder.

[Changes in 0.0.5-alpha](https://github.com/zeroking1998/KF2-Optimizer-Next/releases/tag/v0.0.5-alpha) · [All releases](https://github.com/zeroking1998/KF2-Optimizer-Next/releases)

No installer, Windows service, scheduled task, or account is required. Keep the
executable beside its `Data` folder. The app is currently unsigned, so Windows
SmartScreen may ask you to confirm the first launch.

The official ZIP contains every project-owned runtime file required by that
version. Players do not need Visual Studio, CMake, the KF2 SDK, or a separate
telemetry download.

## What it does

### Changes in Killing Floor 2

- Zed AI reuses reachability checks during navigation. Solo play also enables
  eight Zed optimizations covering sprint audio, targeting, bump handling,
  grabs, King Fleshpound beam targeting and AI registration. Of these eight,
  only sprint audio also runs on joined and listen servers.
- Adaptive starts from your graphics settings and adjusts individual controls
  when it detects pressure. Eligible corpses can sleep after their ragdolls
  settle. Graphics and corpse settings are restored after the session.
- With FleX enabled, eligible render draws avoid CPU vertex packing and
  readbacks can overlap other work. The original DLLs are restored after play.

### What the Optimizer provides

- Set Target FPS (30–240) and Maximum corpses on Home.
- Monitor live FPS, frame time, average FPS, 1% lows and resource usage.
  Loading screens and confirmed Escape/trader menus are excluded from gameplay
  statistics while live FPS continues.
- Manage graphics settings, backups, updates and repair from the portable app.
  Detailed runtime diagnostics are optional.

## Quick start

1. Start KF2 Optimizer.
2. Confirm the detected Killing Floor 2 folder.
3. Choose **Target FPS** and **Maximum corpses** on Home.
4. Leave **Adaptive optimization** on for automatic adjustments, or turn it
   off to keep your saved graphics settings and continue monitoring.
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
