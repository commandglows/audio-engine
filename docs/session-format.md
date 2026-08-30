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
