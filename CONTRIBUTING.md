# Contributing to KF2 Optimizer Next

Thank you for helping. The project welcomes bug reports, documentation fixes,
tests, and focused code changes that preserve its safety boundary.

## Before opening an issue

Search existing issues, read [Support](SUPPORT.md), and remove personal paths or
identifiers from logs. Use the provided bug or feature template.

## Before writing code

Read the [developer guide](docs/DEVELOPER_GUIDE.md),
[code style](docs/CODE_STYLE.md), and [safety model](docs/SAFETY.md). Discuss a
large architectural or runtime-provider change in an issue first.

## Pull-request requirements

- `main` is protected. Every change must use a pull request with a current
  branch, successful `Test Debug` and `Test Release` checks, and resolved review
  conversations.
- Use clear English in code, comments, UI, logs, tests, and documentation.
- Keep the change focused; do not mix unrelated cleanup with behavior changes.
- Add a regression test for changed behavior and relevant failure paths.
- Preserve truthful proposed/applied/readback semantics and exact restoration.
- Preserve bounded runtime work and the permanent no-gameplay-mutation boundary.
- Update user documentation and `CHANGELOG.md` for user-visible behavior.
- Run Debug and Release tests plus documentation validation.

```powershell
pwsh -NoProfile -File tools/test.ps1 -Configuration Debug
pwsh -NoProfile -File tools/test.ps1 -Configuration Release
pwsh -NoProfile -File tools/validate_documentation.ps1
```

Provider, telemetry-module, overlay, or packaging changes require the relevant
native Windows/package validation in addition to unit tests.

## Commit and pull-request text

Explain the problem, the chosen boundary, observable behavior, tests performed,
and remaining limitations. Do not claim an action is applied when evidence only
shows that it was proposed or requested.

By contributing, you agree that your contribution will be distributed under
the project’s `GPL-3.0-only` license.
