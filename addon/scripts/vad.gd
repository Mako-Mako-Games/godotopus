extends RefCounted
class_name VoiceActivityDetector

## Amplitude-based voice activity detector with hysteresis (separate attack
## and release thresholds) and a time-based hang period, so a brief dip in
## volume mid-sentence doesn't immediately end the stream and chop one
## utterance into many tiny bursts.
##
## Hang time is tracked by elapsed wall-clock time (delta_sec passed into
## process()), not by counting frames, so it stays correct even if frame size
## or rate changes between configure() calls.

signal speech_started
signal speech_ended

var _threshold: float = 0.02
var _release_threshold: float = 0.012
var _hang_time_sec: float = 0.3

var _is_speaking: bool = false
var _hang_remaining_sec: float = 0.0
var _last_amplitude: float = 0.0

func configure(p_threshold: float, p_release_threshold: float, p_hang_time_sec: float) -> void:
	_threshold = p_threshold
	_release_threshold = p_release_threshold
	_hang_time_sec = p_hang_time_sec

func is_speaking() -> bool:
	return _is_speaking

func get_last_amplitude() -> float:
	return _last_amplitude

## pcm: interleaved PackedFloat32Array for one frame. delta_sec: real time
## elapsed since the previous call (used to drive the hang timer).
## Returns the updated is_speaking state.
func process(pcm: PackedFloat32Array, delta_sec: float) -> bool:
	_last_amplitude = _rms(pcm)

	if not _is_speaking:
		if _last_amplitude >= _threshold:
			_is_speaking = true
			_hang_remaining_sec = _hang_time_sec
			speech_started.emit()
	else:
		if _last_amplitude >= _release_threshold:
			_hang_remaining_sec = _hang_time_sec
		else:
			_hang_remaining_sec -= delta_sec
			if _hang_remaining_sec <= 0.0:
				_is_speaking = false
				speech_ended.emit()

	return _is_speaking

func reset() -> void:
	_is_speaking = false
	_hang_remaining_sec = 0.0
	_last_amplitude = 0.0

func _rms(pcm: PackedFloat32Array) -> float:
	if pcm.is_empty():
		return 0.0
	var sum_sq := 0.0
	for sample in pcm:
		sum_sq += sample * sample
	return sqrt(sum_sq / pcm.size())
