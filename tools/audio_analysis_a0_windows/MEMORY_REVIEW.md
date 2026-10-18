# Private A0 scoped client memory review

AC-A4 scopes 256 KiB to retained analysis context and 128 KiB to an incoming
encoded response (384 KiB together). It does not cap all Flutter/VM memory.
Wire length, native map/codec allocation peaks, client retained objects and
client decode/installation overlap are distinct measurements.

Run engine/private calibration directly, Doppler NONE. For a short retained
view measurement, use the private spike with silent defaults:

```powershell
flutter run -d windows --profile --dart-define=A0_AUTORUN=true --dart-define=A0_AUTORUN_ON_ONLY=true --dart-define=A0_PHYSICAL_STIMULUS=false --dart-define=A0_MASK=11 --dart-define=A0_SECONDS=5
```

Wait for A0_RESULT and leave the stopped diagnostic window open. Then pass its
local VM service URI to `measure_vm_analysis_view.ps1 -VmServiceUri <local-uri>
-OutputPath <new-json>`. Keep the full runtime result and binary/source hashes.
The script requests no GC/reset/evaluation. It inventories the AnalysisView
object and latest/speech data, excluding framework listeners; stable IDs are
managed in one WebSocket session and deleted afterward. Bounded traversal
rejects incomplete sizes, storage or changed roots. Old growable storage leaves
a lower bound; fixed CalibrationHopList storage exposes its 151-slot backing.

The reported VM object graph is not a transient peak, allocator arena size,
incoming codec/batch footprint or the future production context. The validated
14,208-byte silent mask11 post-stop graph must keep those limits. Raw graph
pages remain in build artifacts; compact reviewed evidence is under the dated
A0 evidence directory.

`measure_vm_allocation_profile.ps1` is a separate accumulator experiment. It
requests reset, no service GC, and preserves before/after profiles. On the
observed Profile VM, accumulated fields equal current fields for every class;
that response does not demonstrate retired allocation throughput.

## Synthetic transport object graph

The sibling spike contains `tool/measure_transport_heap.dart`. Start its
fixture explicitly in the spike directory; it is a parked, silent synthetic
experiment and does not enable the physical stimulus or audio channel:

```powershell
flutter run -d windows --profile --dart-define=A0_MEMORY_FIXTURE=true --dart-define=A0_PHYSICAL_STIMULUS=false
```

After startup, use the local VM service WebSocket URI shown by Flutter/DevTools
(the `ws://127.0.0.1:.../.../ws` URI) and save both the JSON report and raw
`.dartheap` snapshot under a new build-artifact name:

```powershell
dart run tool/measure_transport_heap.dart --vm-service-uri=ws://127.0.0.1:PORT/TOKEN/ws --output=build/audio_analysis_a0/transport-heap.json
```

The fixture creates one case for each mask `{3, 5, 11, 13}`. The reader requires
all four cases and five observable roots per case: `priorRecord`, `priorSpeech`,
`incomingWire`, `decodedPacket`, and `parsedBatch`. AOT can discard fields that
are never observably read, so the fixture checksum reads each root; the reader
refuses to call a case complete if a root is missing or cannot be matched to
the snapshot. Its role unions deduplicate shared objects by snapshot identity
before adding shallow sizes; they are not a sum of the per-root summaries.

These totals are VM-reported shallow object bytes plus VM-reported external
properties. They omit the separately counted shared VM/type/compiler metadata
and do not describe allocator capacity, reserved heap, or process RSS. The
synthetic payload covers a bounded native-shaped subset; its wire length is a
lower bound for a full native response. `requestHeapSnapshot` can perturb the
isolate and trigger collection. The result is a retained synthetic graph at
snapshot time, not transient allocation throughput, a true overlap peak, or
proof of the production AC-A4 cap.

## Actual silent native-channel capture

The spike also has a private actual-channel capture page. From the spike
directory, run it with the physical stimulus and synthetic fixture both off:

```powershell
flutter run -d windows --profile --dart-define=A0_NATIVE_MEMORY_CAPTURE=true --dart-define=A0_PHYSICAL_STIMULUS=false --dart-define=A0_MEMORY_FIXTURE=false
```

Each mask runs as its own bounded silent experiment. It warms a 150-hop prior
view, waits about 120 ms, then makes up to 32 polls at the same cursor within a
3-second sampling budget. Short contiguous replies are discarded; only an
exact 16-record adjacent reply without resync is parked. Poll timings, record
counts, decisions, endpoint identity, and stop/native metrics are included in
the bounded case log. The capture does not install candidate batches into the
prior view. The first 200 ms attempt returned a resync reply and was preserved
as incomplete evidence; that result does not establish a full 16-record
capture. Check the latest runtime log before claiming one.

Each case exposes six selected roots: the prior latest record and speech
history, the exact `ByteData` envelope, a diagnostic `Uint8List.view` over that
same buffer (no additional byte copy by the capture), the MethodChannel-decoded
map, and the parsed batch. The codec still performs its normal decode work.
Cases also retain capture status and endpoint/native metrics, but those scalar
metadata are outside the selected payload graph. Four masks are separate held
experiments; their totals are not one combined production context or a
four-mask aggregate.

After all cases finish, leave the window open and use the local VM service
WebSocket URI with the sibling heap reader:

```powershell
dart run tool/measure_transport_heap.dart --vm-service-uri=ws://127.0.0.1:PORT/TOKEN/ws --output=build/audio_analysis_a0/transport-heap-actual.json
```

The selected graph remains a snapshot-time parked object graph. It is not a
temporal transport peak, complete production context, allocator-capacity
measurement, or proof of the AC-A4 limits. `requestHeapSnapshot` can perturb
the isolate and trigger collection; the report preserves these limits and
marks any missing roots or incomplete capture accordingly.

The reviewed actual-channel run is now recorded in the dated evidence directory:
`native-transport-runtime.json`, `vm-native-transport-graph.json` and
`vm-native-transport-review.json`. All four masks yielded adjacent 16-record
replies, with no resync, prior delivery loss, exceptions, submission/queue
failure, worker failure or failed stop. The largest selected union is 284,880
bytes at mask11, including 109,824 reported external bytes; its exact encoded
envelope is 109,809 bytes. These measurements keep the parked-graph limits above.

The VM reports immutable typed-data wrappers as PlainInstance with length0 and
no visible fields. Zero therefore cannot establish their logical wire length.
The capture persists `actualEncodedEnvelopeBytes` directly from the original
ByteData; the reader records this length source, rejects missing/zero/conflicting
lengths, and requires a positive exact length <=128 KiB for a complete case.
The earlier v2 reader report's zero lengths are rejected metadata, not a cap
pass. The runtime summary and final graph exact lengths agree per mask.
