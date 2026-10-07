---
artifact: business_context
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: audio-engine
created: "2026-09-02"
updated: "2026-09-02"
status: reviewed
source_skill: 300-sg-docs
scope: business
owner: Diane
confidence: high
risk_level: high
security_impact: medium
docs_impact: yes
target_audience: "ShipGlows product teams that need dependable native recording infrastructure."
value_proposition: "Provide one bounded native audio contract that ShipGlows products can reuse across supported platforms."
business_model: "Private shared product infrastructure; value is realized through the ShipGlows products that consume it."
market: "Internal cross-platform audio capture infrastructure."
delivery_posture: development
evidence:
  - README.md
  - PITCH.md
  - shipglows_data/technical/contracts/verification.md
  - "Operator decision 2026-09-02: delivery_posture is development."
linked_artifacts:
  - PITCH.md
  - shipglows_data/technical/code-docs-map.md
depends_on: []
supersedes: []
next_review: "2026-12-02"
next_step: "/sg-docs audit"
---

# Business Context

## Business Identity

ShipGlows Audio Engine is private shared infrastructure for native recording capabilities used by ShipGlows products. Its durable role is to isolate real-time audio concerns behind a bounded product-facing contract.

## Primary Customer

The evidenced customer is the internal ShipGlows product team integrating recording into Flutter applications. End users benefit through those consuming products rather than using this repository directly.

## Offer And Transformation

The engine replaces product-specific native audio implementations with a portable core, platform backends, structured diagnostics, and a stable Flutter boundary.

## Business Model

The repository has no standalone commercial offer. Its value is reduced duplication and better recording reliability across ShipGlows products.

## Decisions, Hypotheses And Unknowns

- Confirmed: `delivery_posture` is `development`.
- Evidence-backed: Windows and Android paths exist with explicitly bounded verification evidence.
- Unknown: readiness for each future consuming product and device class remains subject to its verification matrix.

## Risks

- Build success can be mistaken for device-level recording proof.
- Real-time regressions or platform fragmentation could affect every consuming product.
