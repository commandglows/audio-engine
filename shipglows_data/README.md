---
artifact: project_governance
metadata_schema_version: "1.0"
artifact_version: "1.0.2"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-10-01"
status: reviewed
source_skill: sg-docs
scope: project-governance
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: medium
security_impact: none
docs_impact: yes
linked_systems:
  - README.md
  - technical/contracts/
  - flutter/shipglows_audio/
  - tests/
depends_on: []
supersedes: []
evidence:
  - "Repository audit on 2026-08-31 found an established technical docs set but no canonical ShipGlows governance corpus."
  - "2026-10-01: recorded the operator's decision to build and maintain the shared audio engine in-house."
next_review: "2026-11-30"
next_step: "Maintain the technical map when a mapped subsystem or validation route changes."
---

# Audio Engine Governance

This directory is the canonical internal navigation and governance corpus for
this repository. Its `technical/contracts/` documents are the human-readable
technical contracts migrated from the former root documentation directory.

## Canonical Entry Points

- `workflow/specs/shared-multi-app-audio-engine.md`: shared capability contract and consumer boundary.
- `technical/code-docs-map.md`: path-to-document and validation routing.
- `technical/audio-engine-research.md`: architecture patterns, real-time rules, platform strategy, open-source study candidates, licenses, and prototype gates for the shared DAW-capable engine.
- `technical/contracts/engine-ownership-decision.md`: selected direction to build and maintain the shared engine in-house.
- `technical/context.md`: operational system overview.
- `technical/context-function-tree.md`: structural entrypoint map.
- `technical/`: subsystem context and behavior-recovery documents.

## Scope Boundary

The engine is private infrastructure. Product-facing integration copy remains
in the root README and `flutter/shipglows_audio/README.md`; no public editorial
corpus is declared for this repository.

## Maintenance Rule

Update this corpus whenever a mapped code area, validation route, or durable
technical decision changes. Preserve non-redundant content before consolidating
any future document.
