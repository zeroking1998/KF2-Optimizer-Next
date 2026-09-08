# Legacy recovery repository audit

This report accounts for the private legacy recovery repository without
changing or deleting it. The comparison target is public `origin/main` at
`6eb0d339124905b2343c6ad48a1109091a227ff0`.

## Result

No unique product source needs to be copied from the recovery repository.

- The recovery branch is 44 commits ahead of its bundle base. Every commit is
  either represented by the initial public source import, superseded by later
  public work, or is a recovery-only planning/cleanup record that remains safe
  in Git history.
- Of 134 modified or untracked project files outside `out/`, 117 have an exact
  blob in the public repository history.
- The 17 non-identical files are one generated telemetry package binary and 16
  files from an older UI animation integration. The current public tree contains
  the same behavior in a newer implementation, including startup, page,
  tooltip, press, hover, slider, exit, and update-glow motion.
- Nine paths present in the recovery project but absent from current public
  `main` were all previously public and intentionally removed or replaced.
- `KF2Optimizer/out/` contains 14,642 generated files (about 2.23 GiB).
  `video-review-overlay-61/` contains 25 user-created review images (about
  53.4 MiB). Neither group is product source.

## Commit inventory

The public initial import is
[`21b7e21`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/21b7e21438c6835ea4bc6a0d49ae7e636bd8e51f).
Later public cleanup and replacements are linked where they materially changed
the imported structure.

| Recovery commit | Subject | Classification |
| --- | --- | --- |
| `bed7f6f` | design safe code cleanup | Recovery planning record; retained in recovery Git history |
| `d490bff` | plan safe code cleanup | Recovery planning record; retained in recovery Git history |
| `4267fbd` | remove archived legacy source trees | Intentional removal of obsolete `Quellcode/`, `Tests/`, and `R24Runtime/` trees |
| `ed2528d` | split game log session implementation | Migrated in the public initial import and subsequently evolved |
| `85526c9` | split overlay window implementation | Migrated in the public initial import and subsequently evolved |
| `05487a7` | split application runtime implementation | Migrated in the public initial import and subsequently evolved |
| `70d7baf` | design modular extensible architecture | Recovery planning record; implementation migrated |
| `7bd934f` | clarify action access policies | Recovery planning record; implementation migrated |
| `8e95131` | plan typed action architecture | Recovery planning record; implementation migrated |
| `78f0e22` | align action plan with strict TDD | Recovery planning record; tests and implementation migrated |
| `9293219` | clarify guide reset action mapping | Recovery planning record; action behavior migrated |
| `038bcb0` | add typed action and control catalog | Migrated and subsequently evolved |
| `43bc4e2` | centralize action routing policy | Migrated and subsequently evolved |
| `353107d` | type application control routing | Migrated and subsequently evolved |
| `70b2ec4` | register Adaptive online UI action | Migrated and subsequently evolved |
| `aa31f0f` | preserve existing action ID values | Migrated and covered by the current typed action contract |
| `da69307` | require cataloged UI actions | Migrated and covered by current tests |
| `6bda467` | describe typed UI command boundary | Exact public-history copy exists |
| `8f49532` | correct typed action inventory | Recovery planning record; implementation migrated |
| `28fb473` | plan feature action modules | Recovery planning record; implementation migrated |
| `6a372de` | stabilize action ownership metadata | Migrated and subsequently evolved |
| `ab05af1` | introduce typed feature action registry | Migrated and subsequently evolved |
| `bb78254` | extract navigation actions | Migrated, then intentionally consolidated by [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |
| `0eafca4` | extract guide actions | Migrated and subsequently evolved |
| `3b669c5` | extract overlay actions | Migrated and subsequently evolved |
| `d6afbd4` | extract diagnostics actions | Migrated and subsequently evolved |
| `7a65adc` | extract backup actions | Migrated and subsequently evolved |
| `af55db9` | extract optimizer actions | Migrated, then intentionally consolidated by [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |
| `dffd217` | extract settings actions | Migrated and subsequently evolved |
| `1832ffd` | extract game actions | Migrated and subsequently evolved |
| `026173c` | complete feature action modules | Migrated and subsequently evolved |
| `78daf7c` | stabilize overlay focus validation | Exact public-history copy exists |
| `c81140f` | plan typed telemetry pipeline | Recovery planning record; implementation migrated |
| `e21ad7c` | introduce typed telemetry frame | Migrated and subsequently evolved |
| `cadd618` | extract telemetry session stage | Migrated and subsequently evolved |
| `6c132d5` | extract telemetry FleX stage | Migrated and subsequently evolved |
| `2b2d10c` | collect one telemetry frame per tick | Migrated and subsequently evolved |
| `00db651` | extract telemetry Adaptive stage | Migrated and subsequently evolved |
| `ac5f042` | isolate telemetry effects | Migrated and subsequently evolved |
| `aea13fc` | extract telemetry presentation stage | Migrated and subsequently evolved |
| `66b1ab5` | complete typed telemetry pipeline | Migrated and subsequently evolved |
| `27b5494` | clarify telemetry effect boundary | Exact public-history copy exists |
| `f175e40` | remove obsolete manual optimizer mode | Migrated; the public product remains Adaptive-only |
| `9a60c88` | unify FleX quality under Adaptive control | Migrated and subsequently evolved |

## Dirty project files

The 117 exact matches comprise 82 modified tracked files and 35 untracked files.
Git object identity was checked against the complete public history, not only the
current checkout. This covers the application, configuration, Adaptive policy,
telemetry stages, documentation, tests, publication files, and build tools.

<details>
<summary>82 modified files with an exact public-history match</summary>

```text
assets/offline_telemetry/kf2optimizertelemetryprobe.uc
docs/architecture.md
docs/FINAL_ACCEPTANCE.md
docs/FLEX_ADAPTIVE_SCOPE.md
docs/function-matrix.md
docs/ISSUE_72_PRODUCT_MATRIX.md
docs/PROJECT_STATUS.md
include/kf2/app/application.hpp
include/kf2/config/kf2_catalog.hpp
include/kf2/config/settings.hpp
include/kf2/flex/flex_adaptive_policy.hpp
include/kf2/game/game_log_session.hpp
include/kf2/game/gameplay_log_lab.hpp
include/kf2/game/offline_telemetry_lab.hpp
include/kf2/optimizer/adaptive_governor.hpp
include/kf2/optimizer/adaptive_profile.hpp
include/kf2/optimizer/adaptive_registry.hpp
include/kf2/telemetry/present_source.hpp
include/kf2/ui/ui_model.hpp
README.md
src/app/application_actions.cpp
src/app/runtime/action_contract.cpp
src/config/kf2_catalog.cpp
src/config/setting_catalog.cpp
src/config/settings.cpp
src/diagnostics/feature_inventory.cpp
src/features/diagnostics/diagnostics_actions.cpp
src/features/game/game_actions.cpp
src/features/settings/settings_actions.cpp
src/features/settings/settings_actions.hpp
src/features/telemetry/telemetry_adaptive_stage.cpp
src/features/telemetry/telemetry_adaptive_stage.hpp
src/features/telemetry/telemetry_effect_stage.cpp
src/features/telemetry/telemetry_effect_stage.hpp
src/features/telemetry/telemetry_flex_stage.cpp
src/features/telemetry/telemetry_flex_stage.hpp
src/features/telemetry/telemetry_session_stage.cpp
src/flex/flex_adaptive_policy.cpp
src/game/game_log_telemetry.cpp
src/game/gameplay_log_lab.cpp
src/optimizer/adaptive_governor.cpp
src/optimizer/adaptive_profile.cpp
src/optimizer/adaptive_registry.cpp
src/optimizer/optimizer_engine.cpp
src/overlay/overlay_policy.cpp
src/telemetry/present_source.cpp
src/ui/ui_model.cpp
tests/architecture/action_module_boundary_test.cmake
tests/architecture/telemetry_pipeline_boundary_test.cmake
tests/integration/application_lifecycle_test.cpp
tests/integration/backup_restore_test.cpp
tests/integration/present_source_test.cpp
tests/unit/action_contract_test.cpp
tests/unit/action_router_test.cpp
tests/unit/adaptive_governor_test.cpp
tests/unit/adaptive_profile_test.cpp
tests/unit/adaptive_registry_test.cpp
tests/unit/feature_inventory_test.cpp
tests/unit/feature_registry_test.cpp
tests/unit/flex_adaptive_policy_test.cpp
tests/unit/flex_audit_test.cpp
tests/unit/game_log_session_test.cpp
tests/unit/ini_document_test.cpp
tests/unit/kf2_catalog_test.cpp
tests/unit/overlay_policy_test.cpp
tests/unit/setting_catalog_test.cpp
tests/unit/settings_test.cpp
tests/unit/telemetry_adaptive_stage_test.cpp
tests/unit/telemetry_effect_stage_test.cpp
tests/unit/telemetry_flex_stage_test.cpp
tests/unit/ui_model_test.cpp
tests/windows/automation_provider_test.cpp
tests/windows/direct2d_renderer_test.cpp
tests/windows/flex_forwarder_loader_test.cpp
tests/windows/gameplay_log_lab_test.cpp
tests/windows/offline_telemetry_lab_test.cpp
tools/config_roundtrip_validator.cpp
tools/generate_release_evidence.ps1
tools/package.ps1
tools/test.ps1
tools/validate_acceptance_ledger.ps1
tools/validate_release.ps1
```

</details>

<details>
<summary>35 untracked files with an exact public-history match</summary>

```text
.editorconfig
.gitattributes
.github/ISSUE_TEMPLATE/bug_report.yml
.github/ISSUE_TEMPLATE/config.yml
.github/ISSUE_TEMPLATE/feature_request.yml
.github/PULL_REQUEST_TEMPLATE.md
.github/workflows/windows-ci.yml
.gitignore
assets/offline_telemetry/KF2OptimizerTelemetryViewport.uc
assets/PROVENANCE.md
CHANGELOG.md
CODE_OF_CONDUCT.md
CONTRIBUTING.md
docs/BINARY_DISTRIBUTION.md
docs/CODE_STYLE.md
docs/DEVELOPER_GUIDE.md
docs/FEATURE_REFERENCE.md
docs/GLOSSARY.md
docs/HOW_IT_WORKS.md
docs/OPEN_SOURCE_CHECKLIST.md
docs/README.md
docs/SAFETY.md
docs/USER_GUIDE.md
include/kf2/optimizer/adaptive_actuation.hpp
include/kf2/optimizer/adaptive_stability.hpp
LICENSE
ROADMAP.md
SECURITY.md
src/optimizer/adaptive_actuation.cpp
SUPPORT.md
tests/unit/adaptive_actuation_test.cpp
THIRD_PARTY_NOTICES.md
tools/build_kf2_telemetry.ps1
tools/validate_documentation.ps1
tools/validate_publication.ps1
```

</details>

The only non-identical working files are classified below.

| Paths | Classification | Preservation decision |
| --- | --- | --- |
| `assets/offline_telemetry/KF2OptimizerTelemetry.u` | Generated KF2 SDK package binary | Do not copy as source; rebuild from the maintained telemetry source |
| `include/kf2/ui/ui_animation.hpp`, `src/ui/ui_animation.cpp`, `tests/unit/ui_animation_test.cpp` | Older uncommitted animation implementation | Superseded by the current public animation module and tests |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | Wiring for that older UI implementation plus other already-migrated work | Superseded by current public build and test manifests |
| `include/kf2/ui/direct2d_renderer.hpp`, `include/kf2/ui/shell_controller.hpp` | Older animation integration surface | Superseded by current renderer/controller interfaces |
| `src/ui/direct2d_renderer.cpp`, `src/ui/shell_controller.cpp`, `src/ui/shell_layout.cpp` | Older startup, page, tooltip, control, slider, close, and update-glow motion | Behavior exists in the current public UI and has continued to evolve |
| `src/app/application.cpp`, `src/app/application_runtime.cpp`, `src/app/application_runtime.hpp`, `src/app/application_window.cpp` | Older UI/runtime wiring plus settings migration and Adaptive tracking | UI wiring is superseded; settings migration and Adaptive tracking exist in current public source |
| `tests/unit/shell_controller_test.cpp`, `tests/unit/shell_layout_test.cpp` | Tests for the older layout/controller variant | Superseded by current public tests |

## Recovery-only paths absent from current main

These paths are not unique or lost. Each exists in public Git history.

| Path | Evidence and disposition |
| --- | --- |
| `assets/offline_telemetry/KF2OptimizerTelemetry.u` | Generated binary; source-controlled public versions exist |
| `assets/offline_telemetry/KF2OptimizerTelemetryViewport.uc` | Imported publicly, then replaced by the current telemetry probe in [`8cb4d75`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/8cb4d75) |
| `include/kf2/benchmark/benchmark.hpp` | Imported publicly, then removed as unreachable legacy code in [`b7de52b`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/b7de52b) |
| `src/benchmark/benchmark.cpp` | Imported publicly, then removed as unreachable legacy code in [`b7de52b`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/b7de52b) |
| `tests/unit/benchmark_test.cpp` | Imported publicly, then removed with the unreachable benchmark code in [`b7de52b`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/b7de52b) |
| `src/features/navigation/navigation_actions.cpp` | Imported publicly, then consolidated in [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |
| `src/features/navigation/navigation_actions.hpp` | Imported publicly, then consolidated in [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |
| `src/features/optimizer/optimizer_actions.cpp` | Imported publicly, then consolidated in [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |
| `src/features/optimizer/optimizer_actions.hpp` | Imported publicly, then consolidated in [`89b8545`](https://github.com/zeroking1998/KF2-Optimizer-Next/commit/89b8545) |

## Non-source data

| Location | Kind | Current action |
| --- | --- | --- |
| `KF2Optimizer/out/` | Generated build, test, package, and runtime output | Keep untouched until cleanup is explicitly approved |
| `video-review-overlay-61/` | User-created screenshots and contact sheets | Preserve; never treat as disposable build output |
| `.git/` | Recovery history and object database | Preserve |
| `docs/superpowers/` | Historical implementation plans and design notes | Preserve in recovery history; no product-doc copy needed |

## Proposed cleanup list

This is a proposal only. Nothing was removed or moved during the audit.

1. Archive or delete `KF2Optimizer/out/` only after confirming no package or log
   is still needed for an active investigation.
2. Move `video-review-overlay-61/` to a user-selected archive location, or keep
   it in place. Do not delete it as generated output.
3. Keep the recovery Git repository until this report is reviewed and all open
   work is confirmed against the public repository.
4. If the entire recovery repository is later retired, first create a final
   bundle or read-only archive and verify that it can be opened.

No cleanup action is authorized by this report.
