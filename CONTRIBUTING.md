# Contributing to KF2 Optimizer Next

Thank you for helping. Small fixes are welcome, and you do not need to know the
whole project before contributing.

## Choose a contribution

- Found a problem? Open a [bug report](https://github.com/zeroking1998/KF2-Optimizer-Next/issues/new/choose).
- Have an idea? Open a feature request before starting a large change.
- Want to improve text or documentation? A focused pull request is enough.
- Want to change behavior? Add a test that proves the new behavior.

Never post private paths, account details, or unedited logs in a public issue.

## Set up the project

Follow the [Build Guide](docs/BUILDING.md). The short version is:

```powershell
pwsh -NoProfile -File ./tools/check_requirements.ps1
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Debug
```

## Make a change

1. Create a branch from the correct base branch.
2. Keep the change focused on one problem.
3. Use clear English in code, comments, UI, logs, tests, and documentation.
4. Add or update a focused test for changed behavior.
5. Update `CHANGELOG.md` and user documentation when users will notice the change.

The [Developer Guide](docs/DEVELOPER_GUIDE.md) explains the code map and safety
rules. [Code Style](docs/CODE_STYLE.md) covers naming and comments.

## Test before opening a pull request

```powershell
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File ./tools/test.ps1 -Configuration Release
pwsh -NoProfile -File ./tools/validate_documentation.ps1
```

Telemetry, overlay, configuration, or packaging changes may need the additional
checks listed in [Testing](docs/testing.md).

## Open the pull request

Use the pull-request template and explain:

- what was wrong or missing;
- what changed for the user;
- how you tested it;
- what still needs a real KF2 or Windows test.

GitHub CI runs the Debug and Release suites automatically. A request or proposal
is not an applied runtime action unless a matching acknowledgement or exact
readback proves it.

By contributing, you agree that your contribution is distributed under the
project's `GPL-3.0-only` license.
