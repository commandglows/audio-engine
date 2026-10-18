---
artifact: technical_docs_map
metadata_schema_version: "1.0"
artifact_version: "1.4.1"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-10-13"
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
  - "2026-10-06: mapped the standalone deterministic shared-DSP test project and the post-limiter callback-period proof gap."
next_review: "2026-11-30"
next_step: "Update this map with every new subsystem or validation route."
---

# Code Documentation Map

| Code area | Primary context | Contract / reader doc | Focused validation |
| --- | --- | --- | --- |
| `native/engine/` | `session-integrity.md` | `contracts/session-format.md`, `contracts/v0.2-contract.md`, `contracts/engine-ownership-decision.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | `ctest --test-dir build/native -C Debug --output-on-failure` |
| `flutter/shipglows_audio/native/engine/src/offline_wav_analysis.cpp`, `src/offline_sidecar.cpp`, `tools/audio_analysis_cli/` | `../workflow/specs/shared-audio-analysis-and-visualization.md` (draft A6 contract) | `native/engine/include/shipglows/audio/offline_wav_analysis.hpp`, `tools/audio_analysis_cli/README.md` | Build CLI and focused CTest targets; `scale_smoke.ps1` verifies 6s/180s music+speech and a 1h speech file (3600 chunks/15 pages, 6,676,480-byte process peak working set). Server authorization, hostile concurrent root races, A0 and production integration remain separate |
| `tools/visualization_fixture/`, `flutter/shipglows_visualization/tool/scene_fixture_parity.dart` | `../workflow/specs/shared-audio-analysis-and-visualization.md` (draft A7) | `tools/visualization_fixture/README.md`, `flutter/shipglows_visualization/lib/src/scene.dart` | Native analysis-v2 random seeks, all-frame TypeScript/Dart scene parity, and local Edge sole-WAV WebM mux; A0 calibration, physical timing, and product exporter proof remain open |
| `windows/` | `platform-adapters.md` for capture; `windows-playback.md` for playback | `contracts/architecture.md`, `contracts/verification.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | Windows capture integration and Debug build; separately validate playback decoder, effect commands, typed output errors, and named-endpoint runtime |
| `android/src/main/cpp/` | `platform-adapters.md` | `contracts/architecture.md`, `recording-recovery-index.md` | Android integration test and multi-ABI APK build |
| `lib/`, platform bridges | `platform-adapters.md` | `flutter/shipglows_audio/README.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | `flutter analyze`, `flutter test` |
| `tests/` | `recording-recovery-index.md` | `contracts/verification.md` | native CTest suite |
| Shared DSP, offline render worker, DAW playback, and engine ownership | `audio-engine-research.md` | `../workflow/specs/shared-multi-app-audio-engine.md`, `contracts/engine-ownership-decision.md`, ContentGlows worker architecture/render contract | Prove offline processing in the Linux worker, then shared DSP equivalence and Windows/Android realtime gates |
| `flutter/shipglows_audio/windows/windows_wasapi_playback.cpp`, `windows/shipglows_audio_plugin.cpp`, `native/engine/src/dsp_chain.cpp` | `windows-playback.md` | `../workflow/specs/shared-multi-app-audio-engine.md`, Flutter package README | Configure `flutter/shipglows_audio/native/engine/tests` with CMake in a temporary build directory, then run CTest; also run focused Flutter/channel tests and the Windows plugin build. Endpoint evidence records errors, underruns, callback p99 and maximum, endpoint name, and device period |
| `flutter/shipglows_audio/windows/windows_mf_decoder.cpp`, `tests/decoder_trials/` | `windows-playback.md` | `tests/decoder_trials/README.md`, `../workflow/specs/shared-multi-app-audio-engine.md` | `cmake -S tests/decoder_trials -B build/decoder-trials -G "Visual Studio 17 2022" -A x64`, build Release, then `ctest --test-dir build/decoder-trials -C Release` |
| `flutter/shipglows_audio/example/integration_test/` | `platform-adapters.md`, `../workflow/specs/shared-audio-analysis-and-visualization.md` | `flutter/shipglows_audio/README.md`, `contracts/verification.md` | Windows playback/analysis integration; opt-in A0 start/poll/stop test when the private CMake flag is enabled |
| Private `tools/audio_analysis_a0/`, optional Windows A0 observer and sibling `beatglows/spikes/audio_analysis_a0/` | `windows-playback.md` | `../workflow/specs/shared-audio-analysis-and-visualization.md`, native/spike README, `../workflow/evidence/audio-analysis-a0/2026-10-07/`, `../../tools/audio_analysis_a0_windows/ETL_REVIEW.md`, `../../tools/audio_analysis_a0_windows/MEMORY_REVIEW.md`, `../../tools/audio_analysis_a0_windows/PHYSICAL_TIMING.md` | Direct native reference test, generated PCM, Windows profile evidence, plus callable private A0 handler test. These do not prove calibrated physical lag or aggregate transient allocation cap; A0 and A8 readiness remain open |

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
