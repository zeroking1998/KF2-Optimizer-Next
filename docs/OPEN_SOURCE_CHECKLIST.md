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

## Required before calling the project open source

- [ ] Confirm the chosen project name and public repository owner
- [ ] Complete independent binary-release redistribution review
- [ ] Configure branch protection and required Windows CI checks
- [ ] Add repository topics, description, and a first signed release

## Publication boundary

Publish `KF2Optimizer` as the root of a fresh repository. Do not push the
enclosing workspace Git history: its removed historical objects include
old ZIP/EXE releases, an original FleX runtime, large audit datasets, and
German-only legacy planning documents that are not part of the active product.

The initial GitHub repository is source-only. Do not upload the portable binary
package as a public Release until the
[binary distribution review](BINARY_DISTRIBUTION.md) is complete.

## License decision

The project owner selected **GPL-3.0-only**. This is intentionally version 3
only, not the `GPL-3.0-or-later` variant. Keep the complete root `LICENSE` and
the packaged `Data\Documentation\LICENSE` copy unchanged.
