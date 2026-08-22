# Portable updates

KF2 Optimizer Next can update itself from the project's official GitHub
Releases without installing a service, scheduled task, installer, or permanent
background process.

## For users

Select **Updates** in the upper-right corner to run a manual check. When a
newer version is available, the same button changes to **Update available**,
receives a visible highlight, and starts the consent-based installation flow.
**Optimization → Version & Updates** shows the installed version, available
version, last check and current status. Automatic checks are enabled by default
and run at startup no more than once in 24 hours. Turning them off does not
disable the upper-right manual button.

When a newer semantic version exists, the page shows its publication date,
download size and concise release notes. Nothing is downloaded or installed
until **Install update** is selected. **Later** hides the offer for the current
session.

Internet and GitHub errors do not stop the optimizer. They remain quiet in the
Updates status and can be retried manually.

## Verification and installation

The updater accepts only the exact Windows-x64 ZIP named for the selected
version in the configured official repository. Before installation it checks:

- repository, tag, version, asset name and official GitHub download URL;
- exact release size and GitHub-provided SHA-256 digest;
- the extracted package version and build identity;
- every file in the package integrity manifest.

The download and extraction happen in a new isolated temporary folder. A copy
of the running executable becomes the temporary helper. After the main app
closes, that helper backs up all managed program files, replaces them
atomically, verifies the installed package and starts the new executable.

The new app must complete startup and return a token-bound readiness receipt.
If download, verification, replacement or restart fails, the helper restores
the verified backup and starts the previous version. Temporary update files
are removed after the helper exits.

## Files that remain unchanged

Only the fixed managed program-file list from the package manifest is updated.
Portable user state is not part of that list. In particular, the updater keeps
settings, update-check state, logs, backups, benchmarks, profiles, session
recovery data, FleX lab state and offline telemetry lab state.

## Developer boundaries

The implementation is split into semantic versioning, GitHub release parsing,
release-note filtering, update state, controller policy, archive verification,
managed-file transactions, the temporary helper and UI integration. Each
boundary has focused tests. Release-facing changelogs must contain only
**What's new**, **Bug fixes**, and optional **Important notes**.
