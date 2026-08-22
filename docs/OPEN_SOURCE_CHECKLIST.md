# Open-Source Publication Checklist

This file separates repository preparation from the legal and publishing
decisions that require an owner.

## Ready in the repository

- [x] English entry page and documentation index
- [x] User, architecture, safety, development, and contribution guides
- [x] Issue and pull-request templates
- [x] Code of conduct, security policy, support guide, changelog, and roadmap
- [x] Automated Windows build/test workflow
- [x] Documentation-link and English-contract validation
- [x] Third-party PresentMon license retained
- [x] Root `GPL-3.0-only` license selected and included in packages
- [x] Standalone `.gitignore`, `.gitattributes`, third-party notices, and asset
  provenance added
- [x] Enclosing history audited and rejected for publication because it
  contains removed binaries, archives, proprietary runtime files, and large
  historical audit data
- [x] Locally SDK-compiled telemetry package excluded from the public source
  repository

## Current publication state

- [x] Confirm project name `KF2 Optimizer Next` and public repository owner
- [x] Configure repository topics, description, issues, and Windows CI
- [x] Protect `main` with required pull requests, current Debug/Release checks,
  resolved review conversations, linear history, and blocked force-push/delete
- [x] Publish the source repository with a fresh, reviewed history
- [x] Publish the owner-authorized `v0.0.4-alpha` portable pre-release

## Remaining release-hardening work

- [ ] Complete independent binary-release redistribution review
- [ ] Obtain a trusted code-signing certificate and publish a signed release

## Publication boundary

Publish `KF2Optimizer` as the root of a fresh repository. Do not push the
enclosing workspace Git history: its removed historical objects include
old ZIP/EXE releases, an original FleX runtime, large audit datasets, and
German-only legacy planning documents that are not part of the active product.

Generated binaries remain outside source control. The owner-authorized portable
alpha is distributed only through GitHub Releases and follows the documented
[binary distribution boundary](BINARY_DISTRIBUTION.md).

## License decision

The project owner selected **GPL-3.0-only**. This is intentionally version 3
only, not the `GPL-3.0-or-later` variant. Keep the complete root `LICENSE` and
the packaged `Data\Documentation\LICENSE` copy unchanged.
