package com.commandglows.shipglows_audio

import android.content.Context
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

    // The MethodChannel that will the communication between Flutter and native Android
    //
    // This local reference serves to register the plugin with the Flutter Engine and unregister it
    // when the Flutter Engine is detached from the Activity
    private lateinit var channel: MethodChannel
    private lateinit var audioManager: AudioManager

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        audioManager =
            flutterPluginBinding.applicationContext.getSystemService(Context.AUDIO_SERVICE)
                as AudioManager
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
                val inputDeviceId = call.argument<Int>("inputDeviceId") ?: -1
                if (inputDeviceId >= 0 && inputDevices().none { it.id == inputDeviceId }) {
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
                val status = parseStatus(nativeCommand(1, directory, inputDeviceId))
                if (status["state"] == "failed") {
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
                if (inputDeviceId == null || inputDevices().none { it.id == inputDeviceId }) {
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
                        result.success(parseStatus(nativeCommand(5, null, inputDeviceId)))
                    }
                }
            }
            "stopRecording" -> {
                runNativeCommand(2, result)
                clearCommunicationRoute()
            }
            "pauseRecording" -> runNativeCommand(3, result)
            "resumeRecording" -> runNativeCommand(4, result)
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
        clearCommunicationRoute()
        channel.setMethodCallHandler(null)
    }
}
