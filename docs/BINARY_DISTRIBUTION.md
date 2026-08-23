# Binary Distribution Boundary

## Public source repository

The public repository may contain the project’s C++, PowerShell, CMake, and
UnrealScript source under `GPL-3.0-only`, the original project artwork, and the
separately licensed Intel PresentMon source subset.

It must not contain:

- the KF2 game or SDK;
- original KF2 assets or binaries;
- KF2's original NVIDIA FleX runtime;
- the locally generated `KF2OptimizerTelemetry.u` package;
- old executables, archives, private audit datasets, or enclosing Git history.

The compiled telemetry package is excluded through `.gitignore`. Contributors
install the official KF2 SDK separately, accept its applicable terms, and run
`tools/build_kf2_telemetry.ps1`. Packaging automatically invokes that build
when the module is absent and refuses to create a reduced-function package.

## Local package

The local packaging and runtime-validation flow requires the locally compiled
telemetry package. KFEditor can emit binary differences between equivalent
source builds, so the Release executable embeds the SHA-256 of the exact local
module selected for that package. Packaging rejects a mismatched executable
and module and never produces a reduced-function package. This preserves
complete local testing and does not grant permission to publish that binary.

## Public binary release

The project owner authorized the free public `v0.0.4-alpha` portable package.
It contains the project executable, the project-built FleX laboratory
forwarder, the hash-pinned telemetry module compiled from the published
UnrealScript source, integrity metadata, and license documentation.

It does not contain the KF2 game or SDK, original KF2 assets or binaries, or
KF2's original NVIDIA FleX runtime. Users must provide a legitimate installed
copy of Killing Floor 2. The release remains an unsigned alpha and is clearly
identified as an unofficial community project.

An independent redistribution and GPL-compatibility review remains recommended
before treating binary publication policy as final. Review the installed
KF2/UDK agreement, the compiled telemetry module, the project forwarder's
relationship to the separately installed FleX runtime, and corresponding-source
obligations before changing the release contents.

This document records a conservative publication boundary; it is not legal
advice.
