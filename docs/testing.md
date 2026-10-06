# Testing

Run commands from the repository root.

## Normal contributor checks

```powershell
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Release
pwsh -NoProfile -File ./tools/validate_documentation.ps1
```

`test.ps1` builds before running the tests. GitHub CI runs the same Debug and
Release suites with desktop-only checks excluded.

Test and catalog sizes are recorded once in the
[validation inventory](ISSUE_72_PRODUCT_MATRIX.md#current-validation-inventory).
Documentation validation checks them against source registrations/definitions
and rejects stale duplicated totals in current status documents. Counts describe
the default full Windows build; they do not turn skips or historical results
into passes.

## Choose additional checks by change

| Changed area | Additional command |
|---|---|
| UI or layout | `./tools/validate_gui.ps1 -Configuration Release` |
| FPS telemetry or overlay | `./tools/validate_telemetry_overlay.ps1 -Configuration Release` |
| KF2 configuration | `./tools/validate_config_roundtrip.ps1 -ConfigRoot <path> -Configuration Release` |
| Telemetry module | `./tools/build_kf2_telemetry.ps1` |
| Portable package | `./tools/package.ps1`, then `./tools/validate_release.ps1` |
| Public repository files | `./tools/validate_publication.ps1` |
| Full native foundation | `./tools/validate_foundation.ps1` |

Prefix each script with `pwsh -NoProfile -File` when running it from a normal
PowerShell terminal.

Desktop presentation changes use the existing controller, renderer, window,
and lifecycle tests: node stability during numeric animation, exact UI
Automation range readback, visibility/timer transitions, resource creation
counts, and device-loss recovery. `validate_gui.ps1` checks the complete capture
set and repeated-image determinism; private before/after hashes can additionally
verify pixel-identical output without committing baseline images or benchmarks.

## CI failure diagnosis

The existing resource-worker test records its last log discovery/read and
publication boundaries only in test builds. Failed assertions print the
boundary, inspected timestamps/metadata, raw Windows last-error, request/current
generation and whether a chunk was queued. A metadata rejection does not imply
the raw last-error describes its cause. Normal app builds contain none of this
state or recording work. The test's explicit `--initial-log-open-failure` mode
holds a denied-read lease and must exit with failure; it validates CI failure
output, not a retry or a passing gameplay check. The historical intermittent
failure in #685 remains unconfirmed until its actual failing boundary is captured.

The update-helper test observes asynchronous work-directory deletion with an
error-aware five-second wait. Only a successful status read confirming absence
passes; uncertain status or an existing directory at the deadline fails. No
full-test retry or production update/cleanup behavior is changed.

## Real KF2 checks

Automated tests cannot prove every in-game effect. Changes to protected runtime
providers, corpse behavior, FleX, LOD, or restoration also need a controlled
offline KF2 session when practical. Record native KF2 state separately from:

1. an optimizer proposal;
2. a requested action;
3. a matching acknowledgement or exact readback;
4. restoration after KF2 closes.

Do not report a runtime action as applied if only the request is visible.

## Configuration roundtrip safety

`validate_config_roundtrip.ps1` never edits the supplied KF2 configuration
folder. It copies only allowed INI files below `out`, tests preview, apply,
backup, verification, and restore, then compares the original SHA-256 values.
A missing real configuration reports `BLOCKED`, not `PASS`.
