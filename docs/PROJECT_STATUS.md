# KF2 Optimizer Next - current project status

- Updated: 2026-10-04
- Default branch: `main`
- Development release line: `0.0.4-alpha`; unreleased changes are in the
  [changelog](../CHANGELOG.md).

## Current source contracts

The product is a portable native Windows application with protected,
session-bound KF2 providers. Home, Game graphics, Overlay, Advanced settings,
Debug, and Help & Repair share one settings and status model.

Adaptive uses the user's saved graphics as its baseline, not a named startup
profile. Sensors and actuators report their own verified capabilities;
unavailable telemetry or missing readback does not become a successful action.
Settings, protected session files, updates, and recovery retain transactional
validation and restoration.

The generated Issue 72 inventory distinguishes implemented contracts,
evidence-gated work, external blockers, and permanent safety exclusions.
A catalog entry or capability proposal is not proof of an applied game change.

## Current validation inventory

Test and catalog totals have one checked source:
[the validation inventory](ISSUE_72_PRODUCT_MATRIX.md#current-validation-inventory).
`tools/validate_documentation.ps1` rejects stale totals against the CMake
registrations and native catalog definitions. Inventory sizes are not pass counts.

Fresh Debug, Release, CI, package, and runtime results belong to the exact
source identity recorded in each PR or release evidence. A complete package
also requires the source-fingerprint-verified SDK module, matching executable
identity, and every managed payload hash.

## Historical and external evidence

Earlier official-map, FleX, graphics, and corpse observations are historical
evidence, not acceptance of a newly built executable. The
[acceptance ledger](FINAL_ACCEPTANCE.md) keeps these observations separate from
fresh automated proof and pending gameplay checks.

Physical HDR, mixed DPI, multi-monitor/multi-GPU variants, Windows security
policies, code signing, and long soaks require their actual target environment.
They remain external gates when that environment is unavailable.
