# Developer Guide

## Repository map

| Path | Responsibility |
|---|---|
| `include/kf2` | Public C++ interfaces grouped by subsystem |
| `src/app` | Application lifecycle, actions, session orchestration |
| `src/config` | Discovery, preview, backup, transactional write, restore |
| `src/telemetry` | Performance collection and normalized samples |
| `src/adaptive` | Evidence windows, pressure, policy, and bounded decisions |
| `src/game` | KF2-specific discovery and protected offline telemetry |
| `src/overlay` | External overlay policy and window behavior |
| `src/ui` | English UI model, layout, accessibility, and rendering |
| `src/diagnostics` | Capability and evidence reporting |
| `tests/unit` | Fast component contracts |
| `tests/windows` | Native Windows, packaging, and integration validation |
| `assets/offline_telemetry` | Source and locally built protected telemetry module |
| `tools` | Build, test, package, validation, and evidence scripts |
| `docs` | User, architecture, safety, status, and acceptance documents |

## Build

Use an x64 Visual Studio 2022 environment with CMake 3.28 or newer and
PowerShell 7.

```powershell
pwsh -NoProfile -File tools/build.ps1 -Configuration Debug
pwsh -NoProfile -File tools/build.ps1 -Configuration Release
```

Do not add generated `out` content to a source change unless a release process
explicitly requires a generated evidence artifact.

### Complete KF2 telemetry build

Install the official KF2 SDK through Steam and accept its applicable terms.
Then compile the project-authored UnrealScript package locally:

```powershell
pwsh -NoProfile -File tools/build_kf2_telemetry.ps1
```

The script discovers the normal Steam SDK location or accepts `-SdkRoot`. It
temporarily stages only this project's six `.uc` files in the installed SDK,
invokes `KFEditor.exe make -useunpublished`, copies the generated module into
the local ignored asset path, and restores `KFEngine.ini`, any previous
unpublished module, and SDK staging state. KFEditor requires the previous
hash-pinned module as its bootstrap/conformity seed. The script uses the local
ignored module by default; a clean checkout can pass `-SeedModule` pointing to
the module from the latest complete release. It refuses to run while KF2 or
KFEditor is open.

## Test

```powershell
pwsh -NoProfile -File tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File tools/test.ps1 -Configuration Release
pwsh -NoProfile -File tools/validate_documentation.ps1
```

Use the smallest relevant test during development, then run the full Release
suite before calling a change complete. Runtime-provider changes also require
the official KF2 SDK compile path, module identity checks, packaging, and a real
Windows/KF2 boundary test where practical.

## Package

```powershell
pwsh -NoProfile -File tools/build_kf2_telemetry.ps1
pwsh -NoProfile -File tools/package.ps1
pwsh -NoProfile -File tools/validate_release.ps1
```

Packaging must preserve portable user data, include third-party notices, record
hashes, and never silently replace protected user state. KFEditor output can
contain compiler-generated binary differences between otherwise equivalent
builds. Therefore packaging compiles or selects the local module first, embeds
that exact module's SHA-256 into the Release executable, and rejects any
executable/module mismatch. If the ignored local module is absent, packaging
invokes the telemetry build and fails closed when the official SDK is
unavailable.

## Change workflow

1. Read the nearest architecture, safety, and status documentation.
2. Reproduce the behavior or define the observable contract.
3. Add or update a focused regression test.
4. Make the smallest behavior-preserving or explicitly requested change.
5. Verify the focused test, full Release suite, and relevant package/runtime
   boundary.
6. Update user documentation and the changelog when behavior is visible.

## Evidence rule

Keep these concepts separate in code, tests, UI, and logs:

```text
observed native state != proposed optimizer action != confirmed applied action
```

An applied result requires the provider’s matching acknowledgement or a verified
readback. A test that only proves a command was sent is incomplete.

## Compatibility and safety

Preserve the permanent boundary in [Safety](SAFETY.md), the target-FPS range of
30 through 240 in one-FPS steps, the corpse ceiling of 4 through 2000, bounded
work, protected restoration, and explicit Unavailable states.
