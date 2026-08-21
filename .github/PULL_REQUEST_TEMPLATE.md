## Problem

What observable problem does this change solve?

## Change

Describe the smallest relevant behavior change and its ownership boundary.

## Evidence

- [ ] Focused regression test
- [ ] Debug test suite
- [ ] Release test suite
- [ ] Documentation validation
- [ ] Native Windows/package/runtime validation when relevant

List exact commands and results.

## Safety and restoration

- [ ] Native state, proposals, requests, and applied receipts remain distinct.
- [ ] Runtime work, retries, queues, and retained state remain bounded.
- [ ] Protected state has a tested restoration path.
- [ ] The change does not modify competitive gameplay.

## Documentation

- [ ] User-visible behavior and `CHANGELOG.md` are updated, or not applicable.
- [ ] New UI, logs, identifiers, comments, tests, and docs use clear English.
