# Session format v1

Each recording session is a private directory containing:

- `manifest.sga`: atomically replaced text manifest;
- `segment-NNNNNN.pcm`: headerless native PCM segments;
- `manifest.tmp`: transient replacement file, never authoritative.

The manifest declares `shipglows-audio-session/1`, completion state, sample
rate, channel count, bytes per sample, and every finalized segment's frame
count. PCM segments are immutable after finalization.

Recovery enumerates segments in lexical order, truncates only incomplete
trailing frame bytes, ignores empty segments, and reports both recovered frames
and discarded bytes. Export into WAV/BWF, FLAC, AAC, or Opus is a later,
non-destructive operation; the capture master is never rewritten in place.

## Session format v2

New captures use `journal.sga` and independently playable
`segment-NNNNNN.wav` files. The append-only journal records the negotiated
format, continuous frame range, byte length, SHA-256 digest, and completion
event. A session is complete only when `event=session_complete` is durably
appended after the final segment is closed and hashed.

Every WAV segment has a finalized RIFF header and can be inspected or played
without the journal. Version 1 remains a supported recovery input; it is never
rewritten in place during migration.
