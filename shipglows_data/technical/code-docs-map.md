---
artifact: technical_docs_map
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: path-to-document-routing
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - context.md
  - context-function-tree.md
  - contracts/
depends_on: []
supersedes: []
evidence:
  - "Built from the release-candidate code and validation surfaces on 2026-08-31."
next_review: "2026-11-30"
next_step: "Update this map with every new subsystem or validation route."
---

# Code Documentation Map

| Code area | Primary context | Contract / reader doc | Focused validation |
| --- | --- | --- | --- |
| `native/engine/` | `session-integrity.md` | `contracts/session-format.md`, `contracts/v0.2-contract.md` | `ctest --test-dir build/native -C Debug --output-on-failure` |
| `windows/` | `platform-adapters.md` | `contracts/architecture.md`, `recording-recovery-index.md` | Windows integration test and Debug build |
| `android/src/main/cpp/` | `platform-adapters.md` | `contracts/architecture.md`, `recording-recovery-index.md` | Android integration test and multi-ABI APK build |
| `lib/`, platform bridges | `platform-adapters.md` | `flutter/shipglows_audio/README.md` | `flutter analyze`, `flutter test` |
| `tests/` | `recording-recovery-index.md` | `contracts/verification.md` | native CTest suite |
| `example/integration_test/` | `platform-adapters.md` | `contracts/verification.md` | `flutter test integration_test/plugin_integration_test.dart -d <device>` |

## Documentation Update Plan

| Changed behavior | Required documents | Owner | Proof |
| --- | --- | --- | --- |
| Capture lifecycle, recovery, route, timestamp, storage, or diagnostics | matching subsystem context, `contracts/architecture.md`, `contracts/v0.2-contract.md`, `contracts/verification.md`, Flutter README when API-visible | executor | focused tests plus applicable platform build/runtime evidence |
| Session journal or segment format | `session-integrity.md`, `contracts/session-format.md`, `contracts/v0.2-contract.md` | executor | CTest and journal/WAV integrity verification |
| Release evidence or a gate change | `contracts/verification.md` and changelog classification | integrator | recorded command/result and explicit remaining gates |

## Non-Coverage

No UI design-system authority is required: this repository’s example is a
diagnostic harness, not a product visual surface.

## Maintenance Rule

Update this map before closing any code change that affects a listed path or
adds a major path.
