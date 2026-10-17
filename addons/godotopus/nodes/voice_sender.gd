class_name VoiceSender
extends Node
## Captures the microphone, encodes it and sends it through a [VoiceTransport].
##
## By default it reads the input device straight from the [AudioServer]
## (with [code]audio/driver/enable_input[/code] on). Set [member capture_bus]
## to read from an [AudioEffectCapture] instead, e.g. to run the microphone
## through bus effects first. Frames Opus classifies as
## silence aren't sent, so a quiet sender costs no bandwidth.
##
## Use one sender per microphone: two would split the input between them.

## Packets started flowing after silence.
signal speaking_started
## Nothing was sent for [member VoiceEncoderSettings.speaking_timeout_ms].
signal speaking_ended
## RMS level of the captured audio, emitted every frame it's read.
signal level_changed(level: float)

## The encoder preset. Assigning one stores a copy; edit that copy (read this
## property back) to change settings at runtime.
@export var settings: VoiceEncoderSettings:
	set = set_settings

## Where packets go. Without one, audio is captured and encoded but dropped.
@export var transport: VoiceTransport

## Audio bus whose [AudioEffectCapture] provides the microphone. Empty reads
## the input device directly.
@export var capture_bus: StringName = &"":
	set(value):
		capture_bus = value
		_capture_effect = null

## While false, input is still read (so it doesn't pile up) but not sent.
## Handy for push-to-talk.
@export var transmitting: bool = true:
	set(value):
		if transmitting == value:
			return
		transmitting = value
		_pipeline.reset()
		_set_speaking(false)

var _pipeline := VoiceCapturePipeline.new()
var _sequence := 0
var _speaking := false
var _capture_effect: AudioEffectCapture
var _capture_source := Callable()
var _capture_source_rate := 0
var _input_requested := false
var _activated_input := false
var _reported_input_error := false


func _init() -> void:
	# Pausing the game shouldn't freeze voice chat or let input pile up.
	process_mode = PROCESS_MODE_ALWAYS
	if settings == null:
		settings = VoiceEncoderSettings.new()


func _exit_tree() -> void:
	if _activated_input:
		AudioServer.set_input_device_active(false)
	_input_requested = false
	_activated_input = false


func _process(_delta: float) -> void:
	poll()


func set_settings(value: VoiceEncoderSettings) -> void:
	if settings != null and settings.changed.is_connected(_apply_settings):
		settings.changed.disconnect(_apply_settings)
	settings = value.duplicate() if value != null else VoiceEncoderSettings.new()
	settings.changed.connect(_apply_settings)
	_apply_settings()


## Reads whatever the capture source has, and encodes and sends it. Called
## every frame; call it yourself if the node isn't processing.
func poll() -> void:
	var frames := _read_input()
	if frames.is_empty():
		return
	if not transmitting:
		return
	for packet in _pipeline.process(frames):
		if transport != null:
			transport.send(packet, _sequence)
			_sequence = (_sequence + 1) & 0xFFFF
	_set_speaking(_pipeline.is_speaking())
	level_changed.emit(_pipeline.get_level())


## Replaces the microphone with [param source], a [Callable] returning the
## stereo frames ([PackedVector2Array]) captured since its last call, at
## [param mix_rate] Hz. Pass an invalid [Callable] to go back to the microphone.
func set_capture_source(source: Callable, mix_rate: int) -> void:
	_capture_source = source
	_capture_source_rate = mix_rate
	_pipeline.reset()


func is_speaking() -> bool:
	return _speaking


func get_level() -> float:
	return _pipeline.get_level()


## Counters from the capture pipeline: [code]frames_encoded[/code],
## [code]packets_sent[/code], [code]frames_silent[/code],
## [code]dropped_ms[/code], [code]encoder_restarts[/code] and
## [code]errors[/code].
func get_stats() -> Dictionary:
	return _pipeline.get_stats()


func _apply_settings() -> void:
	_pipeline.sample_rate = settings.sample_rate
	_pipeline.channels = settings.channels
	_pipeline.frame_duration_ms = settings.frame_duration_ms
	_pipeline.bitrate = settings.bitrate
	_pipeline.complexity = settings.complexity
	_pipeline.dtx = settings.dtx
	_pipeline.inband_fec = settings.inband_fec
	_pipeline.expected_packet_loss = settings.expected_packet_loss_percent
	_pipeline.dred_duration_ms = settings.dred_duration_ms
	_pipeline.speaking_timeout_ms = settings.speaking_timeout_ms


func _set_speaking(speaking: bool) -> void:
	if speaking == _speaking:
		return
	_speaking = speaking
	if speaking:
		speaking_started.emit()
	else:
		speaking_ended.emit()


func _read_input() -> PackedVector2Array:
	var rate: int
	var frames: PackedVector2Array
	if _capture_source.is_valid():
		rate = _capture_source_rate
		frames = _capture_source.call()
	elif capture_bus != &"":
		var effect := _find_capture_effect()
		if effect == null:
			return PackedVector2Array()
		rate = int(AudioServer.get_mix_rate())
		frames = effect.get_buffer(effect.get_frames_available())
	else:
		# Started on first use, so a sender given another source never
		# opens the device.
		if not _input_requested:
			_input_requested = true
			_activate_direct_input()
		if not _activated_input:
			return PackedVector2Array()
		rate = int(AudioServer.get_input_mix_rate())
		frames = AudioServer.get_input_frames(AudioServer.get_input_frames_available())
	if rate != _pipeline.input_mix_rate:
		_pipeline.input_mix_rate = rate
	return frames


func _activate_direct_input() -> void:
	if not ProjectSettings.get_setting("audio/driver/enable_input", false):
		_report_input_error(
			(
				"VoiceSender: microphone input is disabled. "
				+ "Enable audio/driver/enable_input in the project settings."
			)
		)
		return
	var error := AudioServer.set_input_device_active(true)
	if error != OK:
		_report_input_error(
			"VoiceSender: couldn't start the input device (%s)." % error_string(error)
		)
		return
	_activated_input = true


func _find_capture_effect() -> AudioEffectCapture:
	if _capture_effect != null:
		return _capture_effect
	var bus := AudioServer.get_bus_index(capture_bus)
	if bus < 0:
		_report_input_error("VoiceSender: capture_bus '%s' doesn't exist." % capture_bus)
		return null
	for i in AudioServer.get_bus_effect_count(bus):
		var effect := AudioServer.get_bus_effect(bus, i)
		if effect is AudioEffectCapture:
			_capture_effect = effect
			return effect
	_report_input_error("VoiceSender: capture_bus '%s' has no AudioEffectCapture." % capture_bus)
	return null


func _report_input_error(message: String) -> void:
	if not _reported_input_error:
		_reported_input_error = true
		push_error(message)
