package com.commandglows.shipglows_audio

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
    ): String

    // The MethodChannel that will the communication between Flutter and native Android
    //
    // This local reference serves to register the plugin with the Flutter Engine and unregister it
    // when the Flutter Engine is detached from the Activity
    private lateinit var channel: MethodChannel

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "shipglows_audio")
        channel.setMethodCallHandler(this)
    }

    override fun onMethodCall(
        call: MethodCall,
        result: Result
    ) {
        when (call.method) {
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
                val status = parseStatus(nativeCommand(1, directory))
                if (status["state"] == "failed") {
                    result.error(
                        status["errorCode"] as? String ?: "capture_start_failed",
                        "The native Oboe capture could not start.",
                        null,
                    )
                } else {
                    result.success(status)
                }
            }
            "stopRecording" -> runNativeCommand(2, result)
            "getRecordingStatus" -> runNativeCommand(0, result)
            else -> result.notImplemented()
        }
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
        result.success(parseStatus(nativeCommand(command, null)))
    }

    private fun parseStatus(line: String): Map<String, Any> {
        val fields = line.split('|', limit = 17)
        fun number(index: Int): Long = fields.getOrNull(index)?.toLongOrNull() ?: 0L
        fun decimal(index: Int): Double = fields.getOrNull(index)?.toDoubleOrNull() ?: 0.0
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
            "errorCode" to (fields.getOrNull(9) ?: ""),
            "nativeXruns" to number(10),
            "ringOverflowFrames" to number(11),
            "timestampGapFrames" to number(12),
            "writerStalls" to number(13),
            "routeChanges" to number(14),
            "peakLevel" to decimal(15),
            "rmsLevel" to decimal(16),
        )
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
    }
}
