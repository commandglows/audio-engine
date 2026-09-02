# ShipGlows Audio Engine — Pitch

> Pitch reviewed: 2026-09-02 · Project state: see canonical sources below

ShipGlows Audio Engine is private native audio infrastructure shared by ShipGlows products. It separates a real-time C++ recording engine from Flutter product interfaces so capture, buffering, recovery, and diagnostics can evolve behind one bounded plugin contract.

## Current state

The portable engine, Flutter plugin, Windows WASAPI path, Android Oboe path, native tests, and human-readable contracts exist; each backend's readiness remains limited to the proof recorded in the verification contract and device matrix.

## Navigate

- Business truth: `shipglows_data/business/business.md`
- Product truth: `README.md`
- Current work: `not yet documented`
- Technical map: `shipglows_data/technical/code-docs-map.md`
- Repository guide: `README.md`

## Boundaries

A successful native build proves compilation and packaging only; it does not establish production recording quality or complete device compatibility.
