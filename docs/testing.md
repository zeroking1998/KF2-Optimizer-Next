# Testing

Run the native tests from the repository root:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File KF2Optimizer/tools/test.ps1 -Configuration Release
```

Validate deterministic Direct2D captures:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/validate_gui.ps1 -Configuration Release
```

Validate live Windows telemetry and the external overlay without KF2:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/validate_telemetry_overlay.ps1 `
  -Configuration Release
```

Run the complete native-foundation validation:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/validate_foundation.ps1
```

Create and validate the one-file portable release package:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/package.ps1
pwsh -NoProfile -File KF2Optimizer/tools/validate_release.ps1
```

Validate the product contract against copies of the installed KF2 configuration:

```powershell
pwsh -NoProfile -File KF2Optimizer/tools/validate_config_roundtrip.ps1 `
  -ConfigRoot "$env:USERPROFILE\Documents\My Games\KillingFloor2\KFGame\Config" `
  -Configuration Release
```

The roundtrip validator never modifies the supplied directory. It copies only
allowlisted INI files into a unique directory below `out`, runs preview, apply,
verified backup, and restore there, compares SHA-256 before and after, then
removes only that resolved validation directory. A missing real config reports
`BLOCKED`, never `PASS`.

The validator checks source isolation, the requirements inventory, Debug and
Release tests, deterministic GUI captures at 100/150/200 percent, clean
double-build reproducibility, PE32+ x64 GUI identity, product shape, and
forbidden imports. Integration tests exercise portable state, configuration
discovery, lossless previews, disk and drift guards, journal recovery, restore,
keyboard navigation, a real HWND, UI Automation invocation, and clean shutdown.
The telemetry validator additionally creates a real D3D11/DXGI swap chain,
submits real Presents, consumes them through the production ETW adapter, and
rejects unavailable or fabricated FPS. Overlay policy and a layered-window soak
verify stable resources and deterministic hide/show behavior. Gameplay remains
a separate final acceptance step after all feature phases.
