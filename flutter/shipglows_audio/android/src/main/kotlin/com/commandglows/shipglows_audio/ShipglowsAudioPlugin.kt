package com.commandglows.shipglows_audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
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
            }

            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
                refreshRecoveryRoute()
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
        if (nativeLoaded && synthActive) {
            nativeSynthCommand(2)
            synthActive = false
        }
        selectedInput = null
        audioManager.unregisterAudioDeviceCallback(audioDeviceCallback)
        clearCommunicationRoute()
        channel.setMethodCallHandler(null)
    }
}
