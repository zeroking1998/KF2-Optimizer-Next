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
