# KF2 Optimizer Next

KF2 Optimizer Next is an unofficial, portable Windows companion for
Killing Floor 2. It measures performance, prepares verified settings, manages
an external overlay, and can protect selected cosmetic physics features with
explicit runtime acknowledgements and automatic restoration.

> This project is not affiliated with or endorsed by Tripwire Interactive.

## Download

The current development version is **Alpha 0.0.4**.
Download the ZIP, extract the complete `KF2OptimizerNext` folder, and start
`KF2Optimizer.exe`. Building from source is only required for development.

If a required companion file is later missing or damaged, choose **Repair**
in the upper-right corner. The app
downloads only the release asset for its exact installed version, verifies the
build identity and every SHA-256 value, replaces only missing or damaged files,
and then asks for a restart. It never replaces the running executable. Use
**Import Local Package** as the offline fallback.

The executable is currently unsigned, so Windows SmartScreen may display a
warning on first launch. The release page publishes the ZIP checksum.

## What it does

- Measures live FPS, average FPS, 1% low FPS, frame time, CPU, GPU, and memory.
- Supports any target from 30 to 240 FPS in one-FPS steps.
- Uses measured pressure, scene density, and distance to choose bounded changes.
- Previews, backs up, applies, verifies, and restores supported configuration.
- Provides an external overlay that does not draw inside the game process.
- Protects optional corpse-physics and FleX laboratory actions with capability
  checks, actor correlation, acknowledgements, and cleanup.
- Mirrors KF2's video options in a dedicated **Game graphics** page. Changes
  remain staged until the user applies them, KF2 must be closed, and every
  apply creates a verified restore backup.
- Keeps NVIDIA FleX user-controlled: Adaptive never enables it. The dedicated
  graphics control changes Off / Gibs / Gibs and fluids only after an explicit
  user selection and **Apply graphics**.
- Provides a separate **Advanced settings** page for verified KF2 INI-only
  engine, streaming, rendering and effects values. These manual game settings
  are independent of Adaptive and are staged, backed up, and applied only with
  explicit user approval while KF2 is closed.

It does **not** change damage, health, weapons, enemy AI, spawning, networking,
or competitive gameplay. An action is never reported as applied unless the
responsible provider confirms it or a verified readback proves it.

## How the safety chain works

```text
measurement -> decision -> capability check -> bounded action -> receipt/readback
                                                               -> restore
```

The optimizer keeps proposals separate from confirmed actions. If a provider
is missing, identity cannot be verified, or acknowledgement is unavailable,
the control remains unavailable instead of pretending to work.

## Start here

| You want to... | Read |
|---|---|
| Install and use the application | [User guide](docs/USER_GUIDE.md) |
| Understand the complete control flow | [How it works](docs/HOW_IT_WORKS.md) |
| See every major feature and boundary | [Feature reference](docs/FEATURE_REFERENCE.md) |
| Understand safety and restoration | [Safety model](docs/SAFETY.md) |
| Build, test, or change the code | [Developer guide](docs/DEVELOPER_GUIDE.md) |
| Contribute a change | [Contributing guide](CONTRIBUTING.md) |
| Review licenses and asset ownership | [Third-party notices](THIRD_PARTY_NOTICES.md) |
| Understand source and binary distribution boundaries | [Binary distribution](docs/BINARY_DISTRIBUTION.md) |
| Find a term | [Glossary](docs/GLOSSARY.md) |
| Browse all documentation | [Documentation index](docs/README.md) |

## Quick start

1. Download the portable ZIP from the
   <a href="../../releases">Releases page</a>,
   or build it from source if you are contributing.
2. Keep `KF2Optimizer.exe` beside its `Data` directory.
3. On **Home**, confirm or choose the KF2 folder.
4. On **Home**, select the target FPS and maximum corpse count.
5. Start KF2 through the optimizer when you want session restoration and
   verified runtime capabilities.

The packaged executable is written to
`out/package/KF2OptimizerNext/KF2Optimizer.exe`. The adjacent `Data` directory
contains portable user state and is preserved when the application is updated.

## Build and test

Requirements: Windows 10 or newer, Visual Studio 2022 Build Tools with MSVC
x64 and a Windows SDK, CMake 3.28 or newer, and PowerShell 7. The complete
runtime also requires the separately installed official KF2 SDK. Its compiler
builds the project-authored telemetry module locally; no KF2 SDK file is stored
in this repository.

```powershell
pwsh -NoProfile -File tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File tools/test.ps1 -Configuration Release
pwsh -NoProfile -File tools/validate_documentation.ps1
pwsh -NoProfile -File tools/validate_publication.ps1
pwsh -NoProfile -File tools/build_kf2_telemetry.ps1
pwsh -NoProfile -File tools/package.ps1
```

A clean source checkout uses the telemetry module from the latest complete
release as KFEditor's bootstrap seed:

```powershell
pwsh -NoProfile -File tools/build_kf2_telemetry.ps1 `
  -SeedModule C:\path\to\KF2OptimizerTelemetry.u
```

See the [developer guide](docs/DEVELOPER_GUIDE.md) for architecture, test
layers, packaging, and evidence rules.

## Project status

The project is under active development. The concise current status is in
[PROJECT_STATUS.md](docs/PROJECT_STATUS.md); detailed acceptance evidence is in
[FINAL_ACCEPTANCE.md](docs/FINAL_ACCEPTANCE.md).

## Contributing

Issues and pull requests are welcome. Public text, UI text, code comments,
diagnostics, and new identifiers must use clear English. Start with
[CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_STYLE.md](docs/CODE_STYLE.md).

## License

KF2 Optimizer Next is free and open-source software licensed under the
[GNU General Public License v3.0 only](LICENSE), SPDX identifier
`GPL-3.0-only`. Modified and redistributed versions must preserve the freedoms
and obligations of that license. Third-party components retain their own
licenses. Generated binaries, including `KF2OptimizerTelemetry.u`, remain
excluded from source control. Ready-to-run release packages may include the
hash-pinned telemetry module built from the published UnrealScript source; they
do not include the KF2 game, SDK, or original NVIDIA FleX runtime. See
[Binary distribution](docs/BINARY_DISTRIBUTION.md) and
[Third-party notices](THIRD_PARTY_NOTICES.md).
