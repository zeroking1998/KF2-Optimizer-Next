# Build Guide

This guide takes a new contributor from a clean Windows PC to a tested build.

## 1. Install the requirements

- Windows 10 or newer, x64
- [Git for Windows](https://git-scm.com/download/win)
- [PowerShell 7](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows)
- [CMake 3.28 or newer](https://cmake.org/download/)
- [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/)

When Visual Studio asks which components to install, import the repository's
`.vsconfig` file or select **Desktop development with C++**. This installs the
MSVC x64 compiler, CMake integration, and a Windows SDK.

The official Killing Floor 2 SDK is optional for normal C++ development and
required only for compiling the protected telemetry module and creating a
complete portable package. Install it from Steam under **Library → Tools**.

### Automatic setup

If these tools are not installed, download and extract the repository source
ZIP, then double-click `setup.cmd`. It uses Windows Package Manager to install
or confirm Git, PowerShell, CMake, and Visual Studio C++ Build Tools. Windows
shows the normal installer and administrator prompts. The script installs
nothing silently in the background after it finishes.

The KF2 SDK is not installed by this script. Steam remains the official source.

## 2. Clone and check the PC

```powershell
git clone https://github.com/zeroking1998/KF2-Optimizer-Next.git
cd KF2-Optimizer-Next
pwsh -NoProfile -File ./tools/check_requirements.ps1
```

The check changes nothing. It lists every available or missing tool and tells
you whether the optional KF2 SDK was found.

## 3. Build and test

Double-click `build.cmd`, or use one terminal command:

```powershell
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Release
```

This configures CMake, builds the application and tests, and runs the full test
suite. Output is written to:

```text
out/build/windows-x64-release/Release/KF2Optimizer.exe
```

For a faster development build, replace `Release` with `Debug`.

The one-click command accepts the same options. For example:

```powershell
./build.cmd -Configuration Debug
```

## 4. Build a complete portable package

A ready-to-run package needs the locally compiled telemetry module. The source
repository intentionally does not contain that generated KF2 package.

Install the official KF2 SDK through Steam, close KF2 and KFEditor, then run:

```powershell
./package.cmd
```

On the first build, the script downloads the newest published portable ZIP from
the official GitHub repository, checks its repository, tag, filename, size, and
GitHub SHA-256 digest, and extracts only its telemetry module as the compiler
seed. The official KF2 SDK then compiles the current public UnrealScript source.
Later builds reuse the local ignored module as their seed.

The complete package is written to `out/package/KF2OptimizerNext`. The scripts
bind the application to the newly compiled telemetry hash and refuse to create
a reduced or mismatched package. The one-click flow also validates every
managed file and package hash before reporting success.

For an offline build, provide an extracted official release seed manually:

```powershell
./package.cmd `
  -TelemetrySeedModule "C:\path\to\KF2OptimizerNext\Data\Lab\KF2OptimizerTelemetry.u"
```

## 5. Match GitHub CI

Before opening a pull request, run:

```powershell
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Release
pwsh -NoProfile -File ./tools/validate_documentation.ps1
pwsh -NoProfile -File ./tools/validate_publication.ps1
```

GitHub runs the same Debug and Release test suites for every pull request.
The Release job uploads a temporary Windows x64 developer artifact to the
workflow run. It contains the tested executable and license files, but not the
locally SDK-compiled telemetry package. Official Releases use the complete
package flow from the previous section.

## Common problems

### `pwsh` is not recognized

Install PowerShell 7, close the terminal, and open a new terminal.

### CMake cannot find Visual Studio

Open Visual Studio Installer, choose **Modify**, import `.vsconfig`, and install
the selected C++ components.

### The KF2 SDK is not found

Install **Killing Floor 2 - SDK** through Steam, or pass its game root:

```powershell
pwsh -NoProfile -File ./tools/build_kf2_telemetry.ps1 `
  -SdkRoot "D:\Steam\steamapps\common\killingfloor2"
```

### The telemetry seed is missing

Check the internet connection and GitHub status. For an offline build, extract
an official release and pass its `Data\Lab\KF2OptimizerTelemetry.u` file with
`-TelemetrySeedModule`.

### KF2 or KFEditor is running

Close both programs before compiling telemetry. The script will not modify SDK
staging files while either process is active.
