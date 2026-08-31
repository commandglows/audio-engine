---
artifact: technical_governance
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: technical-governance
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - code-docs-map.md
  - context.md
  - context-function-tree.md
  - contracts/
depends_on: []
supersedes: []
evidence:
  - "Mapped against the repository implementation and release-candidate validation on 2026-08-31."
next_review: "2026-11-30"
next_step: "Run a technical documentation audit after the physical interruption matrix."
---

# Technical Governance

Read `code-docs-map.md` before changing code. It maps every major engine area
to its primary technical context, durable user-facing contract, and focused
validation route.

The C++ implementation is authoritative for current behavior. The documents in
`contracts/` describe durable session and platform contracts; this directory
owns their navigation, maintenance triggers, and explicit proof boundary.

## Security Notes

Session directories and journals are private recording data. Never place
recording contents, device identifiers, tokens, or unredacted runtime logs in
this corpus.

## Maintenance Rule

Keep code routing, invariants, and validation commands synchronized with the
implementation and with `contracts/verification.md`.
