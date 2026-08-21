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

The local packaging and runtime-validation flow requires the locally compiled,
hash-pinned telemetry package. It never produces a reduced-function package.
This preserves complete local testing and does not grant permission to publish
that binary.

## Public binary release

Do not upload the portable package as a GitHub Release until an independent
review confirms that every binary and asset may be distributed together under
compatible terms. At minimum, review the installed KF2/UDK agreement, the
compiled telemetry package, the optional FleX forwarder/runtime relationship,
and corresponding-source obligations.

This document records a conservative publication boundary; it is not legal
advice.
