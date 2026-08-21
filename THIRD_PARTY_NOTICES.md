# Third-Party Notices

KF2 Optimizer Next is licensed under `GPL-3.0-only`. The following separate
components, products, and names retain their own licenses and ownership.

## Intel PresentMon source subset

The source under `third_party/presentmon` is derived from Intel PresentMon and
is distributed under its permissive license:

> Copyright (C) 2017-2024 Intel Corporation

The complete license is in
[`third_party/presentmon/LICENSE.txt`](third_party/presentmon/LICENSE.txt) and is
also included in portable packages as `Data/Documentation/PresentMon-LICENSE.txt`.

## Killing Floor 2 and Tripwire Interactive

Killing Floor 2, its SDK, game binaries, content, names, and trademarks belong
to their respective owners. They are not licensed by this project. KF2
Optimizer Next is an unofficial independent project and is not affiliated with
or endorsed by Tripwire Interactive.

The public source repository does not include the KF2 game, the KF2 SDK,
original game assets, or the locally compiled
`assets/offline_telemetry/KF2OptimizerTelemetry.u` package. That package is
generated from the adjacent project-authored UnrealScript source and requires a
separately installed official KF2 SDK. It is intentionally ignored until its
public binary-distribution terms receive an independent review. See
[`docs/BINARY_DISTRIBUTION.md`](docs/BINARY_DISTRIBUTION.md).

## NVIDIA FleX

NVIDIA FleX names and technology belong to NVIDIA and their respective owners.
The repository does not redistribute KF2's original `flexRelease_x64.dll`.
The optional project forwarder is built from project source and activates only
against an independently installed, exact-identity KF2 runtime.

## Project artwork

The mascot animation and sprite sheets under `assets/mascot` are original
project assets, not extracted KF2 textures, meshes, or characters. Their source
and provenance are described in [`assets/PROVENANCE.md`](assets/PROVENANCE.md).
