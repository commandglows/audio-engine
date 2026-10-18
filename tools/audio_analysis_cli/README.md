# Private offline analysis runner

`shipglows_audio_analysis_cli` is a local draft runner for the shared bounded
WAV analyzer and `analysis-v2` sidecar. Manifest version 1 is private and
provisional; it is not a frozen cross-product API.

Invoke it with a caller-authorized, private job root and a relative manifest
path:

```powershell
build\tools\audio_analysis_cli\shipglows_audio_analysis_cli.exe `
  --job-root C:\private\audio-job `
  --manifest analysis-job.json
```

The manifest is a flat ASCII JSON object no larger than 16 KiB. Strings use
printable ASCII without escapes; keys are closed, unique and all required.
The input path is relative to the job root. The output is published to
`jobs/<job_id>/` only after analysis, sidecar read-back validation and result
summary creation complete. The temporary directory is a sibling on the same
volume, so publication uses a no-replace atomic directory rename. `Ctrl+C` and
`SIGTERM` request cancellation; the runner removes only its own staging folder.
The manifest's wall-time budget is explicit and capped at 24 hours.

Required fields:

```json
{
  "schema_version": 1,
  "job_id": "episode-001",
  "input_path": "media/final-mix.wav",
  "sample_count": 777,
  "max_duration_frames": 2880000,
  "max_memory_bytes": 4194304,
  "max_output_bytes": 82544,
  "max_runtime_ms": 60000,
  "block_frames": 4096,
  "feature_mask": 5,
  "input_sha256": "<64 lowercase hex characters>",
  "engine_revision": "<40 or 64 lowercase hex characters>",
  "sensitivity_db": 0,
  "amplitude_gamma": 0.8,
  "attack_ms": 20,
  "release_ms": 180,
  "spectrum_floor_db": -80,
  "spectrum_ceiling_db": 0
}
```

The input must be an admitted float32 stereo 48 kHz RIFF/WAVE file whose digest
and frame count match the manifest. Use masks `3`, `5`, `11` or `13`; the
transform values follow the current draft contract. Exit status is zero only
after publication. Failures emit typed, path-free JSON on stderr; exit status
`130` means cancellation and `124` means the wall-time budget expired.

The caller remains responsible for authorizing the job root and input for its
user or tenant. This local runner does not implement server identity, tenant
authorization, retention policy, or protection against another process that
can modify the caller's job-root directory concurrently. Do not point multiple
tenants at a shared cache root.

## Duration and working-set smoke

`scale_smoke.ps1` generates temporary silent float32 stereo WAVs, hashes them,
and runs the real CLI under an 8 MiB analyzer/writer memory budget. By default
it exercises 6- and 180-second files with music mask 11 and speech mask 5. Add
an explicit one-hour speech run with:

```powershell
.\tools\audio_analysis_cli\scale_smoke.ps1 -DurationsSeconds 3600 -FeatureMasks 5
```

The 2026-10-07 local run published 360,000 records in 3,600 chunks and 15 pages
for the one-hour speech file. It completed in 24.728 seconds; the process peak
working set was 6,676,480 bytes, compared with 6.6 to 7.3 MiB for the 6/180-second
runs under the same 8 MiB analyzer/writer budget. The one-hour output was
32,375,201 bytes. These are synthetic offline scale measurements, not live
device, total allocation throughput, A0 calibration, or production cost proof.
