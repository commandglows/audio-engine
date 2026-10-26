package com.commandglows.shipglows_audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result

/** ShipglowsAudioPlugin */
class ShipglowsAudioPlugin :
    FlutterPlugin,
    MethodCallHandler {
    companion object {
        // SynthEventType values of the native engine.
        private const val SYNTH_NOTE_ON = 0
        private const val SYNTH_NOTE_OFF = 1
        private const val SYNTH_CLICK = 2
        private const val SYNTH_SET_INSTRUMENT = 3

        // Mix analysis source ids: the Dart client sends the Windows contract
        // id, which names the same synth-fenced mixed output on Android.
        private val OUTPUT_MIX_SOURCES = setOf("windows_output_mix_v1", "android_output_mix_v1")
        private const val ANALYSIS_RECORD_LAYOUT = "sgav-record-2-compact-speech-1"
        private const val ANALYSIS_MAX_BLOCK_FRAMES = 16384
        private const val ANALYSIS_MAX_BATCH_RECORDS = 16
        private const val ANALYSIS_MAX_RESPONSE_BYTES = 131072

        // Lets Android settle the new route before the stream is reopened.
        private const val REROUTE_DELAY_MS = 300L
        private const val LOG_TAG = "shipglows_synth"

        private val nativeLoaded: Boolean =
            try {
                System.loadLibrary("shipglows_audio_jni")
                true
            } catch (_: UnsatisfiedLinkError) {
                false
            }
    }

    private external fun nativeCommand(
        command: Int,
        sessionDirectory: String?,
        inputDeviceId: Int,
    ): String

    private external fun nativeSynthCommand(command: Int): String

    private external fun nativeSynthReroute()

    private external fun nativeAnalysisStart(
        featureMask: Int,
        sensitivityDb: Double,
        gamma: Double,
        attackMs: Double,
        releaseMs: Double,
        floorDb: Double,
        ceilingDb: Double,
    ): String

    private external fun nativeAnalysisStop(release: Boolean)

    private external fun nativeAnalysisPoll(
        featureMask: Int,
        afterSequence: Long,
    ): Array<Any?>

    private external fun nativeSynthPost(
        type: Int,
        instrument: Int,
        useInstrument: Boolean,
        accent: Boolean,
        midi: Int,
        velocity: Float,
    ): Boolean

    // Synth session fence, as on Windows: a newer generation takes the output
    // over and commands of any other generation are refused.
    private var synthGeneration = 0L
    private var synthActive = false

    // Mix analysis lease, fenced by the synth generation like on Windows.
    private data class AnalysisLease(
        val generation: Long,
        val leaseId: String,
        val streamId: String,
        val sourceIdentity: String,
        val profile: String,
        val featureMask: Int,
        val config: List<Double>,
        val digest: String,
    )

    private var analysisLease: AnalysisLease? = null
    private var analysisStopped = true
    private var analysisSerial = 0L

    private val mainHandler = Handler(Looper.getMainLooper())
    private val rerouteSynth =
        Runnable {
            if (nativeLoaded && synthActive) nativeSynthReroute()
        }

    // The MethodChannel that will the communication between Flutter and native Android
    //
    // This local reference serves to register the plugin with the Flutter Engine and unregister it
    // when the Flutter Engine is detached from the Activity
    private lateinit var channel: MethodChannel
    private lateinit var audioManager: AudioManager
    @Volatile private var selectedInput: SelectedInput? = null

    private data class SelectedInput(
        val originalId: Int,
        val type: Int,
        val address: String,
        val productName: String,
    )

    private val audioDeviceCallback =
        object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
                refreshRecoveryRoute()
                scheduleSynthReroute(addedDevices)
            }

            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
                refreshRecoveryRoute()
                scheduleSynthReroute(removedDevices)
            }
        }

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        audioManager =
            flutterPluginBinding.applicationContext.getSystemService(Context.AUDIO_SERVICE)
                as AudioManager
        audioManager.registerAudioDeviceCallback(audioDeviceCallback, null)
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "shipglows_audio")
        channel.setMethodCallHandler(this)
    }

    override fun onMethodCall(
        call: MethodCall,
        result: Result
    ) {
        when (call.method) {
            "startSynth", "stopSynth", "noteOn", "noteOff", "click", "setVoice",
            "getSynthStatus" -> handleSynthCall(call, result)
            "getAnalysisCapabilities", "startAnalysis", "getAnalysisBatch", "stopAnalysis" ->
                handleAnalysisCall(call, result)
            "getEngineInfo" ->
                result.success(
                    mapOf(
                        "name" to "ShipGlows Audio Engine",
                        "version" to "0.1.0",
                        "platform" to "android",
                        "backend" to "oboe",
                        "nativeCoreLoaded" to nativeLoaded,
                    ),
                )
            "startRecording" -> {
                if (!nativeLoaded) {
                    result.error(
                        "native_engine_missing",
                        "The native Oboe engine is unavailable.",
                        null,
                    )
                    return
                }
                val directory = call.argument<String>("sessionDirectory")
                if (directory.isNullOrBlank()) {
                    result.error(
                        "invalid_session_directory",
                        "A session directory is required.",
                        null,
                    )
                    return
                }
                val inputDeviceId = call.argument<Int>("inputDeviceId") ?: -1
                val input = inputDevices().firstOrNull { it.id == inputDeviceId }
                if (inputDeviceId >= 0 && input == null) {
                    result.error(
                        "input_device_unavailable",
                        "The selected audio input is not connected.",
                        null,
                    )
                    return
                }
                if (!selectCommunicationRoute(inputDeviceId)) {
                    result.error(
                        "input_device_unavailable",
                        "The selected Bluetooth audio route could not be activated.",
                        null,
                    )
                    return
                }
                selectedInput = input?.let(::selectedInput)
                val status = parseStatus(nativeCommand(1, directory, inputDeviceId))
                if (status["state"] == "failed") {
                    selectedInput = null
                    clearCommunicationRoute()
                    result.error(
                        status["errorCode"] as? String ?: "capture_start_failed",
                        "The native Oboe capture could not start.",
                        null,
                    )
                } else {
                    result.success(status)
                }
            }
            "getInputDevices" ->
                result.success(inputDevices().map(::deviceMap))
            "selectInputDevice" -> {
                val inputDeviceId = call.argument<Int>("inputDeviceId")
                val input = inputDevices().firstOrNull { it.id == inputDeviceId }
                if (inputDeviceId == null || input == null) {
                    result.error(
                        "input_device_unavailable",
                        "The selected audio input is not connected.",
                        null,
                    )
                } else {
                    if (!selectCommunicationRoute(inputDeviceId)) {
                        result.error(
                            "input_device_unavailable",
                            "The selected Bluetooth audio route could not be activated.",
                            null,
                        )
                    } else {
                        selectedInput = selectedInput(input)
                        result.success(parseStatus(nativeCommand(5, null, inputDeviceId)))
                    }
                }
            }
            "stopRecording" -> {
                selectedInput = null
                runNativeCommand(2, result)
                clearCommunicationRoute()
            }
            "pauseRecording" -> runNativeCommand(3, result)
            "resumeRecording" -> runNativeCommand(4, result)
            "getRecordingStatus" -> runNativeCommand(0, result)
            else -> result.notImplemented()
        }
    }

    private fun handleSynthCall(
        call: MethodCall,
        result: Result,
    ) {
        if (!nativeLoaded) {
            result.error("native_engine_missing", "The native Oboe engine is unavailable.", null)
            return
        }
        val generation = (call.argument<Any>("generation") as? Number)?.toLong() ?: 0L
        if (call.argument<Int>("synthApiVersion") != 1 || generation <= 0L) {
            result.error(
                "unsupported_synth_api",
                "Synth API version 1 with a positive generation is required.",
                null,
            )
            return
        }
        if (call.method == "startSynth") {
            val instrument = instrumentCode(call.argument<String>("instrument"))
            if (instrument == null) {
                result.error("invalid_synth_request", "startSynth requires instrument piano or drums.", null)
                return
            }
            if (generation < synthGeneration || (generation == synthGeneration && !synthActive)) {
                result.error("stale_synth_generation", "startSynth requires a newer generation.", null)
                return
            }
            // A new synth session ends the previous session's mix analysis lease.
            if (generation > synthGeneration) releaseAnalysis()
            synthGeneration = generation
            val status = synthStatus(nativeSynthCommand(1))
            if (status["state"] == "error") {
                synthActive = false
                result.error(
                    status["errorCode"] as? String ?: "output_start_failed",
                    "The native Oboe output could not start.",
                    null,
                )
                return
            }
            synthActive = true
            nativeSynthPost(SYNTH_SET_INSTRUMENT, instrument, false, false, 0, 0f)
            result.success(status)
            return
        }
        if (generation != synthGeneration) {
            result.error(
                "stale_synth_generation",
                "The synth command does not match the active generation.",
                null,
            )
            return
        }
        when (call.method) {
            "getSynthStatus" -> {
                result.success(synthStatus(nativeSynthCommand(0)))
                return
            }
            "stopSynth" -> {
                if (synthActive) releaseAnalysis()
                val line = nativeSynthCommand(if (synthActive) 2 else 0)
                synthActive = false
                result.success(synthStatus(line))
                return
            }
        }
        if (!synthActive) {
            result.error("stale_synth_generation", "The synth session is stopped.", null)
            return
        }
        when (call.method) {
            "noteOn", "noteOff" -> {
                val on = call.method == "noteOn"
                val midi = call.argument<Int>("midi") ?: -1
                val velocity = (call.argument<Any>("velocity") as? Number)?.toDouble() ?: 0.0
                val instrumentName = call.argument<String>("instrument")
                val instrument = if (instrumentName == null) 0 else instrumentCode(instrumentName)
                if (midi !in 0..127 || instrument == null ||
                    (on && (velocity.isNaN() || velocity < 0.0 || velocity > 1.0))
                ) {
                    result.error(
                        "invalid_synth_request",
                        "Notes need midi 0..127, velocity 0..1 and an optional instrument.",
                        null,
                    )
                    return
                }
                result.success(
                    nativeSynthPost(
                        if (on) SYNTH_NOTE_ON else SYNTH_NOTE_OFF,
                        instrument,
                        instrumentName != null,
                        false,
                        midi,
                        velocity.toFloat(),
                    ),
                )
            }
            "click" -> {
                val accent = call.argument<Boolean>("accent")
                if (accent == null) {
                    result.error("invalid_synth_request", "click requires a boolean accent.", null)
                    return
                }
                result.success(nativeSynthPost(SYNTH_CLICK, 0, false, accent, 0, 0f))
            }
            "setVoice" -> {
                val instrument = instrumentCode(call.argument<String>("instrument"))
                if (instrument == null) {
                    result.error("invalid_synth_request", "setVoice requires instrument piano or drums.", null)
                    return
                }
                result.success(nativeSynthPost(SYNTH_SET_INSTRUMENT, instrument, false, false, 0, 0f))
            }
            else -> result.notImplemented()
        }
    }

    // Output changes Android may apply by silently rerouting the live stream
    // (Bluetooth headset off: the A2DP-sized stream moves to the speaker), so
    // the stream is reopened to get the new route's low-latency path.
    private fun scheduleSynthReroute(devices: Array<out AudioDeviceInfo>) {
        if (!synthActive || devices.none { it.isSink }) return
        mainHandler.removeCallbacks(rerouteSynth)
        mainHandler.postDelayed(rerouteSynth, REROUTE_DELAY_MS)
    }

    private fun handleAnalysisCall(
        call: MethodCall,
        result: Result,
    ) {
        if (!nativeLoaded) {
            result.error("native_engine_missing", "The native Oboe engine is unavailable.", null)
            return
        }
        val arguments = call.arguments as? Map<*, *>
        val keys = arguments?.keys ?: emptySet<Any?>()
        if (call.argument<Int>("analysisApiVersion") != 2) {
            result.error("unsupported_analysis_api", "${call.method} requires the exact v2 argument map.", null)
            return
        }
        when (call.method) {
            "getAnalysisCapabilities" -> {
                if (keys != setOf("analysisApiVersion")) {
                    result.error("unsupported_analysis_api", "Analysis API v2 requires its exact versioned argument map.", null)
                    return
                }
                result.success(analysisCapabilities())
            }
            "startAnalysis" -> startAnalysis(call, keys, result)
            "getAnalysisBatch" -> {
                val lease = analysisLease
                val generation = (call.argument<Any>("generation") as? Number)?.toLong()
                val after = (call.argument<Any>("afterSequence") as? Number)?.toLong()
                if (keys != setOf("analysisApiVersion", "generation", "leaseId", "afterSequence")) {
                    result.error("unsupported_analysis_api", "getAnalysisBatch requires the exact v2 argument map.", null)
                    return
                }
                if (lease == null || generation != lease.generation ||
                    call.argument<String>("leaseId") != lease.leaseId ||
                    after == null || after < 0L || after > 9007199254740991L
                ) {
                    result.error("stale_analysis_lease", "Analysis lease, generation, or cursor is invalid.", null)
                    return
                }
                val polled = nativeAnalysisPoll(lease.featureMask, after)
                val metadata = polled.getOrNull(0) as? String ?: "!stale_analysis_lease"
                if (metadata.startsWith("!")) {
                    result.error(metadata.substring(1), "The analysis batch could not be read.", null)
                    return
                }
                result.success(analysisBatch(lease, metadata, polled[1] as ByteArray))
            }
            "stopAnalysis" -> {
                val lease = analysisLease
                val generation = (call.argument<Any>("generation") as? Number)?.toLong()
                if (keys != setOf("analysisApiVersion", "generation", "leaseId")) {
                    result.error("unsupported_analysis_api", "stopAnalysis requires the exact v2 argument map.", null)
                    return
                }
                if (lease == null || generation != lease.generation ||
                    call.argument<String>("leaseId") != lease.leaseId
                ) {
                    result.error("stale_analysis_lease", "Analysis lease or generation is stale.", null)
                    return
                }
                if (!analysisStopped) {
                    nativeAnalysisStop(false)
                    analysisStopped = true
                }
                result.success(true)
            }
        }
    }

    private fun startAnalysis(
        call: MethodCall,
        keys: Set<Any?>,
        result: Result,
    ) {
        val base = setOf("analysisApiVersion", "generation", "profile", "featureMask", "transformConfig")
        if (keys != base && keys != base + "source") {
            result.error("unsupported_analysis_api", "startAnalysis requires the exact v2 argument map.", null)
            return
        }
        // Android has no file playback output yet: only the synth mix exists.
        val source = call.argument<String>("source")
        if (source !in OUTPUT_MIX_SOURCES) {
            result.error("unsupported_analysis_source", "The analysis source is not supported on Android.", null)
            return
        }
        val generation = (call.argument<Any>("generation") as? Number)?.toLong() ?: 0L
        if (generation <= 0L || generation != synthGeneration || !synthActive) {
            result.error("stale_analysis_generation", "Analysis generation must match a running synth session.", null)
            return
        }
        val profile = call.argument<String>("profile")
        val mask = call.argument<Int>("featureMask")
        val config = transformConfig(call.argument<Map<*, *>>("transformConfig"))
        val maskValid =
            (profile == "sgav-music-1" && (mask == 3 || mask == 11)) ||
                (profile == "sgav-speech-1" && (mask == 5 || mask == 13))
        if (!maskValid || config == null || profile == null || mask == null) {
            result.error(
                "invalid_analysis_configuration",
                "Profile, feature mask, and transform configuration must match the v2 contract.",
                null,
            )
            return
        }
        val current = analysisLease
        if (current != null && !analysisStopped && current.generation == generation &&
            current.profile == profile && current.featureMask == mask && current.config == config
        ) {
            result.success(leaseMap(current))
            return
        }
        val digest = nativeAnalysisStart(mask, config[0], config[1], config[2], config[3], config[4], config[5])
        if (digest.startsWith("!")) {
            releaseAnalysis()
            result.error(digest.substring(1), "The analysis stream could not start.", null)
            return
        }
        val serial = ++analysisSerial
        val lease =
            AnalysisLease(
                generation = generation,
                leaseId = "analysis-lease-$serial",
                streamId = "android-output-mix-$generation-$serial",
                sourceIdentity = "android-output-mix-v1-$generation",
                profile = profile,
                featureMask = mask,
                config = config,
                digest = digest,
            )
        analysisLease = lease
        analysisStopped = false
        result.success(leaseMap(lease))
    }

    // sensitivity, gamma, attack, release, floor, ceiling; null when outside
    // the v2 contract.
    private fun transformConfig(map: Map<*, *>?): List<Double>? {
        if (map == null || map.size != 7 || (map["version"] as? Number)?.toLong() != 1L) return null
        val values =
            listOf(
                "sensitivity_db",
                "amplitude_gamma",
                "attack_ms",
                "release_ms",
                "spectrum_floor_db",
                "spectrum_ceiling_db",
            ).map { (map[it] as? Number)?.toDouble() ?: return null }
        if (values.any { !it.isFinite() }) return null
        val (sensitivity, gamma, attack, release, floor) = values
        val ceiling = values[5]
        val valid =
            sensitivity in -24.0..24.0 && gamma in 0.25..2.0 && attack in 1.0..200.0 &&
                release in 10.0..2000.0 && floor in -120.0..-20.0 && ceiling in -6.0..12.0 &&
                floor < ceiling
        return if (valid) values else null
    }

    private fun releaseAnalysis() {
        if (analysisLease == null) return
        nativeAnalysisStop(true)
        analysisLease = null
        analysisStopped = true
    }

    private fun leaseMap(lease: AnalysisLease): Map<String, Any> =
        mapOf(
            "analysis_api_version" to 2,
            "generation" to lease.generation,
            "lease_id" to lease.leaseId,
            "stream_id" to lease.streamId,
            "source_identity" to lease.sourceIdentity,
            "profile" to lease.profile,
            "feature_mask" to lease.featureMask,
            "transform_digest" to lease.digest,
            "max_block_frames" to ANALYSIS_MAX_BLOCK_FRAMES,
            "max_batch_records" to ANALYSIS_MAX_BATCH_RECORDS,
            "max_response_bytes" to ANALYSIS_MAX_RESPONSE_BYTES,
        )

    // Maps the native batch metadata (see nativeAnalysisPoll) to the v2 batch.
    private fun analysisBatch(
        lease: AnalysisLease,
        metadata: String,
        records: ByteArray,
    ): Map<String, Any?> {
        val fields = metadata.split('|')
        fun number(index: Int): Long = fields.getOrNull(index)?.toLongOrNull() ?: 0L
        return mapOf(
            "analysis_api_version" to 2,
            "generation" to lease.generation,
            "lease_id" to lease.leaseId,
            "stream_id" to lease.streamId,
            "source_identity" to lease.sourceIdentity,
            "profile" to lease.profile,
            "feature_mask" to lease.featureMask,
            "transform_digest" to lease.digest,
            "state" to (fields.getOrNull(0) ?: "failed"),
            "continuity_id" to number(1),
            "run_origin_frame" to if (number(2) == 1L) number(3) else null,
            "record_layout" to ANALYSIS_RECORD_LAYOUT,
            "first_sequence" to number(4),
            "last_sequence" to number(5),
            "next_sequence_cursor" to number(6),
            "delivery_resync" to (number(7) == 1L),
            "resync_reason" to (fields.getOrNull(8) ?: "none"),
            "skipped_delivery_records" to number(9),
            "pcm_gap" to (number(10) == 1L),
            "dropped_pcm_frames" to number(11),
            "native_observation_ns" to number(12),
            "timing" to null,
            "record_bytes" to number(13).toInt(),
            "record_count" to number(14).toInt(),
            "records" to records,
        )
    }

    private fun analysisCapabilities(): Map<String, Any?> =
        mapOf(
            "analysis_api_version" to 2,
            "available" to true,
            "reason" to null,
            "sources" to
                listOf(
                    mapOf("id" to "android_output_mix_v1", "available" to true, "reason" to null),
                    mapOf("id" to "android_playback_output_v1", "available" to false, "reason" to "playback_unavailable"),
                    mapOf("id" to "wav_host_v1", "available" to false, "reason" to "offline_adapter_unavailable"),
                ),
            "profiles" to
                listOf(
                    mapOf(
                        "id" to "sgav-music-1",
                        "feature_masks" to listOf(3, 11),
                        "record_bytes" to mapOf(3 to 2136, 11 to 6492),
                    ),
                    mapOf(
                        "id" to "sgav-speech-1",
                        "feature_masks" to listOf(5, 13),
                        "record_bytes" to mapOf(5 to 88, 13 to 4444),
                    ),
                ),
            "record_layout" to ANALYSIS_RECORD_LAYOUT,
            "format" to
                mapOf(
                    "sample_rate" to 48000,
                    "channels" to 2,
                    "layout" to "interleaved",
                    "sample_type" to "float32",
                ),
            "max_block_frames" to ANALYSIS_MAX_BLOCK_FRAMES,
            "max_native_bytes" to 4 * 1024 * 1024,
            "history_records" to 128,
            "history_bytes" to 1048576,
            "history_span_frames" to 61440,
            "max_batch_records" to ANALYSIS_MAX_BATCH_RECORDS,
            "max_response_bytes" to ANALYSIS_MAX_RESPONSE_BYTES,
            "transform_versions" to listOf("sgav-visual-transform-1"),
        )

    private fun instrumentCode(name: String?): Int? =
        when (name) {
            "piano" -> 0
            "drums" -> 1
            else -> null
        }

    // Maps the native status line (see AndroidOboeSynthOutput::StatusLine) to
    // the keys ShipglowsSynthStatus.fromMap reads.
    private fun synthStatus(line: String): Map<String, Any> {
        val fields = line.split('|')
        fun number(index: Int): Long = fields.getOrNull(index)?.toLongOrNull() ?: 0L
        fun latency(p50: Int, p99: Int, max: Int): Map<String, Any> =
            mapOf(
                "p50Microseconds" to number(p50),
                "p99Microseconds" to number(p99),
                "maxMicroseconds" to number(max),
            )
        return mapOf(
            "generation" to synthGeneration,
            "state" to (fields.getOrNull(0) ?: "unknown"),
            "errorCode" to (fields.getOrNull(1) ?: ""),
            "instrument" to (fields.getOrNull(2) ?: "piano"),
            "activeVoices" to number(3),
            "peakActiveVoices" to number(4),
            "eventsPosted" to number(5),
            "eventsRejected" to number(6),
            "eventsApplied" to number(7),
            "eventsIgnored" to number(8),
            "stolenVoices" to number(9),
            "sampleRate" to number(10),
            "bufferFrames" to number(11),
            "enginePeriodFrames" to number(12),
            "lowLatency" to (number(13) == 1L),
            "callbacks" to number(14),
            "underruns" to number(15),
            "maxCallbackMicroseconds" to number(16),
            "queueLatency" to latency(17, 18, 19),
            "outputLatency" to latency(20, 21, 22),
            "streamLatencyMicroseconds" to number(23),
            "playbackAttached" to false,
            "outputEndpointName" to "oboe",
        )
    }

    private fun runNativeCommand(
        command: Int,
        result: Result,
    ) {
        if (!nativeLoaded) {
            result.error(
                "native_engine_missing",
                "The native Oboe engine is unavailable.",
                null,
            )
            return
        }
        result.success(parseStatus(nativeCommand(command, null, -1)))
    }

    private fun inputDevices(): List<AudioDeviceInfo> =
        audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS).toList()

    private fun selectCommunicationRoute(inputDeviceId: Int): Boolean {
        val input = inputDevices().firstOrNull { it.id == inputDeviceId }
        if (input?.type != AudioDeviceInfo.TYPE_BLUETOOTH_SCO) {
            clearCommunicationRoute()
            return true
        }
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val output =
                audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).firstOrNull {
                    it.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO &&
                        (it.address == input.address || it.productName == input.productName)
                }
            output != null && audioManager.setCommunicationDevice(output)
        } else {
            @Suppress("DEPRECATION")
            audioManager.startBluetoothSco()
            @Suppress("DEPRECATION")
            audioManager.isBluetoothScoOn = true
            true
        }
    }

    private fun selectedInput(input: AudioDeviceInfo): SelectedInput =
        SelectedInput(
            originalId = input.id,
            type = input.type,
            address = input.address.orEmpty(),
            productName = input.productName.toString(),
        )

    private fun matchesSelectedInput(
        device: AudioDeviceInfo,
        selected: SelectedInput,
    ): Boolean =
        device.type == selected.type &&
            if (selected.address.isNotBlank()) {
                device.address == selected.address
            } else {
                device.id == selected.originalId &&
                    device.productName.toString() == selected.productName
            }

    private fun refreshRecoveryRoute() {
        val selected = selectedInput ?: return
        if (!nativeLoaded) {
            return
        }
        val currentInput = inputDevices().firstOrNull { matchesSelectedInput(it, selected) }
        if (currentInput == null || !selectCommunicationRoute(currentInput.id)) {
            nativeCommand(6, null, -1)
            return
        }
        nativeCommand(6, null, currentInput.id)
    }

    private fun clearCommunicationRoute() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            audioManager.clearCommunicationDevice()
        } else {
            @Suppress("DEPRECATION")
            audioManager.isBluetoothScoOn = false
            @Suppress("DEPRECATION")
            audioManager.stopBluetoothSco()
        }
        audioManager.mode = AudioManager.MODE_NORMAL
    }

    private fun deviceMap(device: AudioDeviceInfo): Map<String, Any> =
        mapOf(
            "id" to device.id,
            "name" to device.productName.toString().ifBlank { deviceType(device.type) },
            "type" to deviceType(device.type),
            "isExternal" to (device.type != AudioDeviceInfo.TYPE_BUILTIN_MIC),
        )

    private fun deviceType(type: Int): String =
        when (type) {
            AudioDeviceInfo.TYPE_BUILTIN_MIC -> "built_in_microphone"
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "bluetooth_sco"
            AudioDeviceInfo.TYPE_USB_DEVICE -> "usb_device"
            AudioDeviceInfo.TYPE_USB_HEADSET -> "usb_headset"
            AudioDeviceInfo.TYPE_WIRED_HEADSET -> "wired_headset"
            else -> "android_$type"
        }

    private fun parseStatus(line: String): Map<String, Any> {
        val fields = line.split('|', limit = 19)
        fun number(index: Int): Long = fields.getOrNull(index)?.toLongOrNull() ?: 0L
        fun decimal(index: Int): Double = fields.getOrNull(index)?.toDoubleOrNull() ?: 0.0
        val errorCode = fields.getOrNull(9) ?: ""
        val recoverable = errorCode == "device_disconnected"
        return mapOf(
            "state" to (fields.getOrNull(0) ?: "unknown"),
            "sampleRate" to number(1).toInt(),
            "channelCount" to number(2).toInt(),
            "sampleFormat" to (fields.getOrNull(3) ?: "unknown"),
            "framesCaptured" to number(4),
            "framesDropped" to number(5),
            "discontinuities" to number(6),
            "clippedSamples" to number(7),
            "deviceRestarts" to number(8),
            "errorCode" to errorCode,
            "errorRecoverable" to recoverable,
            "recoveryAction" to if (recoverable) {
                "automatic_reconnect"
            } else if (errorCode.isEmpty()) {
                "none"
            } else {
                "start_new_session"
            },
            "nativeXruns" to number(10),
            "ringOverflowFrames" to number(11),
            "timestampGapFrames" to number(12),
            "writerStalls" to number(13),
            "routeChanges" to number(14),
            "peakLevel" to decimal(15),
            "rmsLevel" to decimal(16),
            "hardwareTimestamps" to number(17),
            "timestampQueryFailures" to number(18),
        )
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        mainHandler.removeCallbacks(rerouteSynth)
        if (nativeLoaded && synthActive) {
            releaseAnalysis()
            nativeSynthCommand(2)
            synthActive = false
        }
        selectedInput = null
        audioManager.unregisterAudioDeviceCallback(audioDeviceCallback)
        clearCommunicationRoute()
        channel.setMethodCallHandler(null)
    }
}
