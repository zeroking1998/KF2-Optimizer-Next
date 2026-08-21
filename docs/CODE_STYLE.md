# Code and Language Style

## Use clear English everywhere

English is required for source identifiers, comments, UI text, accessibility
labels, logs, diagnostics, tests, documentation, issues, and pull requests.
Prefer common words and short sentences. Define unavoidable KF2-specific terms
in the [glossary](GLOSSARY.md).

## Naming

- Name a type or function for the responsibility it owns, not how it happens to
  be implemented today.
- Use `proposed`, `requested`, `acknowledged`, `applied`, and `restored`
  precisely; do not use them as synonyms.
- Include the unit in names such as `frame_time_ms` and `distance_units`.
- Use positive capability names such as `can_acknowledge` where possible.
- Avoid unexplained abbreviations and generic names such as `data`, `manager`,
  or `handler` when a narrower name exists.

## Comments

Explain why a boundary, invariant, or workaround exists. Do not narrate obvious
syntax. A comment that describes behavior must be updated with the behavior.

## Functions and ownership

Keep functions focused and side effects explicit. Prefer value types and RAII.
Use weak ownership for observed game-world objects unless the component truly
owns their lifetime. Bound queues, retries, per-tick work, and retained history.

## Errors and status

Return actionable context without exposing secrets or personal paths. Do not
convert an unknown, timeout, missing receipt, or failed readback into success.

## Tests

Name tests after observable behavior. Cover boundary values and failure paths.
When production text changes, test the English user-visible contract instead of
duplicating entire paragraphs.

Formatting is defined by `.editorconfig` and the existing C++ conventions in
the nearest file.
