# Portable updates

KF2 Optimizer Next can update itself from the project's official GitHub
Releases without installing a service, scheduled task, installer, or permanent
background process.

## For users

Select **Updates** in the upper-right corner to run a manual check. When a
newer version is available, the same button changes to **Update available**,
receives a visible highlight, and starts the consent-based installation flow.
**Home → Version & Updates** shows the installed version, available version,
last check and current status. The automatic-check toggle is in the upper-right
corner. Automatic checks are enabled by default and run at startup no more than
once in 24 hours. Turning them off does not disable the manual Updates button.
If the system clock moves backward, future saved check or retry timestamps
do not extend the cooldown; a new check can establish a current timestamp.
The last completed result is kept locally, so reopening the app within that
24-hour interval still shows **No newer version available** or the exact newer
version without another network request.

When a newer semantic version exists, the page shows its publication date,
download size and concise release notes in a quiet in-app dialog. Nothing is
downloaded or installed until **Install update** is selected. **Later** hides
the dialog for the current run. **Don't show again** remembers that exact
version and suppresses its dialog on later starts; a newer release still opens
a new dialog. The available version remains visible in Home and in the
highlighted Update button.

Internet and GitHub errors do not stop the optimizer. They remain quiet in the
Updates status and can be retried manually.

If local update state cannot be saved, a visible warning explains that unsaved
check information is session-only. A completed check remains available in the
current app run. **Don't show again** changes neither the ignored version nor
the dialog state until its atomic save succeeds. A successful retry clears only
that persistence warning, not unrelated warnings. Failed checks keep their
attempt/backoff metadata from the initial save without rewriting identical
cache data at completion; a blocked initial save remains explicitly session-only.

## Verification and installation

Update installation, Auto Repair and manual package import use one busy gate.
Repair/import cannot start during an update check or installation, and Install
update cannot start while Auto Repair owns the package. Disabled buttons and
direct action dispatch enforce the same rule. Manual import rechecks after its
folder picker, before changing files. Update checks themselves are read-only
and may run during Repair, but cannot promote to installation until it finishes.
The existing owned repair worker is still joined before shutdown; closing its
window waits for final verification or rollback instead of abandoning a write.

The updater accepts only the exact Windows-x64 ZIP named for the selected
version in the configured official repository. Before installation it checks:

- repository, tag, version, asset name and official GitHub download URL;
- exact release size and GitHub-provided SHA-256 digest;
- the extracted package version and build identity;
- every file in the package integrity manifest.

Auto Repair resolves the installed version through the same official GitHub
metadata and uses the updater's archive preparation path. Missing, malformed,
or mismatched archive digests and sizes block repair before extraction; it never
falls back to an unverified direct download or a different release. Repair uses
its own temporary subfolder and leaves unrelated working-directory files intact.

The download and extraction happen in a new isolated temporary folder. The ZIP
is fully extracted by Windows' synchronous file operation before staged-file
validation or cleanup can run. Copy failures and cancellations stop preparation;
file existence and a fixed delay are never treated as completion. A copy
of the running executable becomes the temporary helper. After the main app
closes, that helper backs up all managed program files, replaces them
atomically, verifies the installed package and starts the new executable.

The new app must complete startup and return a token-bound readiness receipt.
If download, verification, replacement or restart fails, the helper restores
the verified backup and starts the previous version. Temporary update files
are removed only after the identity-bound helper wait confirms exit (including
an absent or reused PID) and the transaction is verified for cleanup. An
unknown process-query/wait result or timeout retains the receipt, journal,
staged package and backup for next-start recovery. A small
`cleanup-deferred.ini` in the temporary update folder records the reason when
writable; it is diagnostic only, not an update failure or a cleanup permit.

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
