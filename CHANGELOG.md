# Changelog

All notable user-visible changes will be documented here. The project currently
uses an **Unreleased** section until the first public version is tagged.

## Unreleased

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

### Changed

- Public UI and diagnostic language is standardized on clear English.
- Adaptive quality pressure uses stronger bounded decisions while preserving
  hysteresis, cooldowns, capability checks, and receipts.

### Fixed

- Telemetry world tracking no longer keeps a completed world alive through a
  strong `ActiveWorld` reference.
- Corpse and FleX capability/readback reporting no longer treats an unconfirmed
  request as applied.
