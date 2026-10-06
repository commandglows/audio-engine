---
artifact: technical_docs_map
metadata_schema_version: "1.0"
artifact_version: "1.3.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-10-06"
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
  - windows-playback.md
depends_on: []
supersedes: []
evidence:
  - "Built from the release-candidate code and validation surfaces on 2026-08-31."
  - "2026-09-30: mapped the shared multi-app engine contract for native and Flutter API changes."
  - "2026-09-30: added the research report that routes shared DAW architecture, real-time, platform, OSS, and license questions."
  - "2026-10-01: mapped the selected in-house C++ engine direction and decision record."
  - "2026-10-03: mapped ContentGlows Linux offline rendering as a shared-core consumer before Flutter realtime playback."
  - "2026-10-06: mapped Windows playback host, Flutter status/effect API, and named-default-endpoint proof to windows-playback.md and the shared multi-app spec."
next_review: "2026-11-30"
next_step: "Update this map with every new subsystem or validation route."
---

# Code Documentation Map

| Code area | Primary context | Contract / reader doc | Focused validation |
| --- | --- | --- | --- |
| `native/engine/` | `session-integrity.md` | `contracts/session-format.md`, `contracts/v0.2-contract.md`, `contracts/engine-ownership-decision.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | `ctest --test-dir build/native -C Debug --output-on-failure` |
| `windows/` | `platform-adapters.md` for capture; `windows-playback.md` for playback | `contracts/architecture.md`, `contracts/verification.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | Windows capture integration and Debug build; separately validate playback decoder, effect commands, typed output errors, and named-endpoint runtime |
| `android/src/main/cpp/` | `platform-adapters.md` | `contracts/architecture.md`, `recording-recovery-index.md` | Android integration test and multi-ABI APK build |
| `lib/`, platform bridges | `platform-adapters.md` | `flutter/shipglows_audio/README.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | `flutter analyze`, `flutter test` |
| `tests/` | `recording-recovery-index.md` | `contracts/verification.md` | native CTest suite |
| Shared DSP, offline render worker, DAW playback, and engine ownership | `audio-engine-research.md` | `../workflow/specs/shared-multi-app-audio-engine.md`, `contracts/engine-ownership-decision.md`, ContentGlows worker architecture/render contract | Prove offline processing in the Linux worker, then shared DSP equivalence and Windows/Android realtime gates |
| `flutter/shipglows_audio/windows/windows_wasapi_playback.cpp`, `windows/shipglows_audio_plugin.cpp`, `native/engine/src/dsp_chain.cpp` | `windows-playback.md` | `../workflow/specs/shared-multi-app-audio-engine.md`, Flutter package README | Focused Flutter/channel and deterministic DSP tests; Windows playback build and endpoint run with error, underrun, callback-p99 and device-period evidence |
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
