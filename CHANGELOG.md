# Changelog

All notable user-visible changes are documented here.

## Unreleased

## 0.0.1-alpha - 2026-08-21

### Added

- Diagnostics now provides an `IMPORT REQUIRED FILES` repair action. The user
  selects a complete matching extracted package, and the app imports only
  missing or damaged companion files after build-identity and SHA-256 checks.

### Security

- Package repair never replaces the running executable, rejects mismatched
  builds and unsafe files, writes atomically, and requires a restart before
  leaving Safe Mode.

### Changed

- Public versioning now uses `Alpha 0.0.x`: this release is displayed as
  `Alpha 0.0.1` and tagged `v0.0.1-alpha`. Future alpha builds increment the
  patch number instead of using labels such as Alpha 2 or Alpha 3.

## 0.1.0-beta.2 - 2026-08-21

### Fixed

- Portable packages with the complete 13-file integrity manifest now start in
  normal mode instead of incorrectly falling back to Safe Mode.
- Release GUI validation now fails if a freshly packaged build rejects its own
  integrity manifest.

## 0.1.0-beta.1 - 2026-08-21

### Added

- Complete English user, architecture, safety, development, and contribution
  documentation for public collaboration.
- GPL-3.0-only project licensing with the complete license in source and
  portable packages.
- GitHub issue and pull-request templates and Windows CI validation.
- Actor-correlated corpse Distance Sleep, Near Wake, and Ragdoll Sleep evidence.
- Duplicate-action suppression for confirmed actor ragdoll state.
- Variable 30–240 FPS targets with live, average, 1% low, frame-time, and scene
  pressure inputs.
- Ready-to-run portable Windows package with integrity manifest and checksum.

### Changed

- Public UI and diagnostic language is standardized on clear English.
- Adaptive quality pressure uses stronger bounded decisions while preserving
  hysteresis, cooldowns, capability checks, and receipts.

### Fixed

- Telemetry world tracking no longer keeps a completed world alive through a
  strong `ActiveWorld` reference.
- Corpse and FleX capability/readback reporting no longer treats an unconfirmed
  request as applied.
- Packaging now continues correctly after building a missing telemetry module.
