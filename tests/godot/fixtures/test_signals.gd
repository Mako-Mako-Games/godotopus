class_name TestSignals
extends RefCounted
## Test signal generators and measurements, mirroring tests/native/signals.hpp.


## Plays a generated signal as a capture source: each call returns the next
## [member chunk] stereo frames, then silence once [member seconds] are used up.
class Source:
	extends RefCounted

	var rate: int
	var chunk: int
	var seconds: float
	var _position := 0
	var _generator: Callable

	func _init(p_generator: Callable, p_rate: int, p_seconds: float, p_chunk := 0) -> void:
		_generator = p_generator
		rate = p_rate
		seconds = p_seconds
		chunk = p_chunk if p_chunk > 0 else p_rate / 50

	## A capture source for [method VoiceSender.set_capture_source].
	func read() -> PackedVector2Array:
		var frames := PackedVector2Array()
		frames.resize(chunk)
		var end := int(seconds * rate)
		for i in chunk:
			var n := _position + i
			if n < end:
				var value: float = _generator.call(float(n) / rate)
				frames[i] = Vector2(value, value)
		_position += chunk
		return frames

	func is_finished() -> bool:
		return _position >= int(seconds * rate)


static func tone(hz: float, amplitude := 0.5) -> Callable:
	return func(t: float) -> float: return amplitude * sin(TAU * hz * t)


## A 140 Hz harmonic series under a 4 Hz syllable envelope. Opus's voice
## detector treats it as speech, unlike a steady tone.
static func voice_like() -> Callable:
	return func(t: float) -> float:
		var value := 0.0
		for h in range(1, 12):
			value += sin(TAU * 140.0 * h * t) / h
		return 0.2 * (0.5 + 0.5 * sin(TAU * 4.0 * t)) * value


## Frequency of the left channel from rising zero crossings, skipping the
## first [param skip] frames.
static func frequency(frames: PackedVector2Array, rate: float, skip := 0) -> float:
	var first := -1.0
	var last := -1.0
	var crossings := 0
	for i in range(skip + 1, frames.size()):
		var a := frames[i - 1].x
		var b := frames[i].x
		if a < 0.0 and b >= 0.0:
			var t := (i - 1) + a / (a - b)
			if first < 0.0:
				first = t
			last = t
			crossings += 1
	if crossings < 2:
		return 0.0
	return (crossings - 1) * rate / (last - first)


## Median of [method frequency] over 50 ms windows, so a few concealed
## frames (whose phase doesn't line up) can't skew it.
static func median_frequency(frames: PackedVector2Array, rate: float) -> float:
	var window := int(rate * 0.05)
	var estimates: Array[float] = []
	for start in range(0, frames.size() - window + 1, window):
		estimates.append(frequency(frames.slice(start, start + window), rate))
	if estimates.is_empty():
		return 0.0
	estimates.sort()
	return estimates[estimates.size() / 2]


## Seconds of audio louder than [param threshold] RMS, in 10 ms windows.
static func audible_seconds(frames: PackedVector2Array, rate: float, threshold := 0.05) -> float:
	var window := int(rate / 100.0)
	var audible := 0
	for start in range(0, frames.size() - window + 1, window):
		var sum := 0.0
		for i in range(start, start + window):
			sum += frames[i].x * frames[i].x
		if sqrt(sum / window) > threshold:
			audible += 1
	return audible / 100.0
