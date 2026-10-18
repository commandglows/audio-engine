# Private A0 heap trace review

Build directly, Doppler NONE:

```powershell
cmake -S tools/audio_analysis_a0 -B build/audio_analysis_a0_msvc
cmake --build build/audio_analysis_a0_msvc --config Release --target audio_analysis_a0_etl
ctest --test-dir build/audio_analysis_a0_msvc -C Release -R a0_etl_ledger_controls --output-on-failure
& build/audio_analysis_a0_msvc/Release/audio_analysis_a0_etl.exe <trace.etl>
```

This offline reader requires no elevation and does not start tracing. It uses
event-header PID attribution and TDH-provided heap property layouts, rejects
incompatible payloads, and counts heap events per PID/type. Its allocation
ledger tracks blocks observed since capture began. Reallocations already
represented by Alloc/Free are not counted twice in live memory. Reallocation
event requested bytes are reported separately from new-block requests.

The observed-live peak excludes allocations already present before capture,
VirtualAlloc and other non-heap paths. Unknown frees are retained as a limit,
not assigned fabricated sizes. This is not global process memory, native
allocator overhead or Dart managed-object accounting. Nonzero rejected layouts
or duplicate live addresses invalidate the ledger for budget approval.

`capture_heap_file.ps1` records for 60 seconds by default (10–120 allowed),
using a PID-targeted file profile. Run it elevated after starting ON in the
private diagnostic. It refuses to replace an existing recording. Preserve the
full A0_RESULT and process/phase timestamps separately. File traces can be
large: the observed 67.43-second trace was approximately 543 MiB. The profile's
8 MiB nominal buffers do not cap disk usage or recorder overhead.
