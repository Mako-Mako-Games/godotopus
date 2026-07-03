extends Resource
class_name VoiceConfig

## Central configuration for a VoiceTransmitter.
##
## Every property validates/clamps on assignment and marks the resource
## dirty on any actual change. VoiceTransmitter polls is_dirty()
## once per _process() and reinitializes if true.
## If the transmitter has multiplayer authority,
## it will also rpc the new settings out via to_dict()/apply_dict() so
## every other peer's matching component reconfigures to match (e.g. you can
## drop bitrate at runtime in response to a peer's connection degrading, and
## everyone listening to that peer picks it up automatically).

signal config_changed

## The working sample rate for the Opus encoder. Input audio will be resampled using Speex
## if necessary. 24000hz is a good balance between clarity and bandwidth.
## As a rule of thumb, the max audible frequency is half the sample rate, so 24000hz gives you a 12khz ceiling, which is fine for voip.
## 48000hz is the max and will give you a 24khz ceiling,
## or "CD quality" audio, but good luck getting a high enough stable bitrate to make full use of it.
## No higher sample rates, sorry if any rats or cats want to speak in ultrasonics.
@export_enum("8000:8000", "12000:12000", "16000:16000", "24000:24000", "48000:48000") var opus_sample_rate: int = 48000:
	set(value):
		if opus_sample_rate == value:
			return
		opus_sample_rate = value
		_mark_dirty()

## 1 or 2 channels. 1 is mono, 2 is stereo. Stereo is 2x the bandwidth and almost never used for voice chat anyways.
## (I will not be extensively testing stereo audio, so if it breaks don't hold me accountable!)
@export_range(1, 2) var opus_channels: int = 1:
	set(value):
		value = clampi(value, 1, 2)
		if opus_channels == value:
			return
		opus_channels = value
		_mark_dirty()

## Bitrate in bits per second to encode the audio at. Lower will save bandwidth, but reduce quality.
## Try and find a good balance for your use case.
## Remember that some people are still running on single digit Mbps connections!
@export_range(2000, 128000, 1000) var opus_bitrate: int = 24000:
	set(value):
		value = clampi(value, 6000, 128000)
		if opus_bitrate == value:
			return
		opus_bitrate = value
		_mark_dirty()

## Encoding quality for the Opus encoder.
## 0-10, where 10 is the most CPU-intensive and highest quality.
## Since we only have one encode stream per user, this should probably be kept at the max of 10 unless
## you have good reason to lower it.
@export_range(0, 10) var opus_complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if opus_complexity == value:
			return
		opus_complexity = value
		_mark_dirty()

## This roughly equates to the number of RPC calls per second.
## Lower values send more smaller packets, whereas higher values send fewer larger packets.
## Since RPC calls have a non-trivial amount of overhead, lower values will use more bandwidth,
## but have lower latency and safer packet loss recovery.
## Think like 1 car crash vs 1 train derailment.
@export_enum("5:5", "10:10", "20:20", "40:40", "60:60") var opus_frame_duration_ms: int = 20:
	set(value):
		if opus_frame_duration_ms == value:
			return
		opus_frame_duration_ms = value
		_mark_dirty()

## Tunes the Opus encoder to add redundancy to each packet to help recover from packet loss.
## Higher values will increase bandwidth usage, but help keep things smooth on lossy connections like WiFi.
## Set to the amount of packets you roughly expect to lose on average.
@export_range(0, 100) var opus_expected_packet_loss_percent: int = 10:
	set(value):
		value = clampi(value, 0, 100)
		if opus_expected_packet_loss_percent == value:
			return
		opus_expected_packet_loss_percent = value
		_mark_dirty()

## VAD amplitude is RMS over a frame, roughly 0.0-1.0 for normalized float PCM.
## Higher values will make the VAD gate more aggressively, lower values will make it gentler.
## (Note, the VAD is very simple right now, this will not work well for all voices or all environments.)
@export_range(0.0, 1.0, 0.001) var vad_threshold: float = 0.02:
	set(value):
		value = clampf(value, 0.0, 1.0)
		if vad_threshold == value:
			return
		vad_threshold = value
		_mark_dirty()

## If the VAD is open, the RMS must drop below this threshold for it to start closing.
## Make sure this is lower than vad_threshold, otherwise the VAD will never close.
@export_range(0.0, 1.0, 0.001) var vad_release_threshold: float = 0.012:
	set(value):
		value = clampf(value, 0.0, 1.0)
		if vad_release_threshold == value:
			return
		vad_release_threshold = value
		_mark_dirty()

## How long to keep the VAD open after the RMS drops below vad_release_threshold.
@export_range(0.0, 2.0, 0.01) var vad_hang_time_sec: float = 0.3:
	set(value):
		value = clampf(value, 0.0, 2.0)
		if vad_hang_time_sec == value:
			return
		vad_hang_time_sec = value
		_mark_dirty()

## How many frames of audio the jitter buffer tries to keep in itself in case of network jitter.
## Lower values will reduce latency, but increase the chance of audio dropouts.
@export_range(1, 10) var jitter_target_depth_frames: int = 3:
	set(value):
		value = clampi(value, 1, 10)
		if jitter_target_depth_frames == value:
			return
		jitter_target_depth_frames = value
		_mark_dirty()

## How many frames of audio the jitter buffer will allow to be buffered before it starts dropping frames.
## This is a safety valve to prevent the jitter buffer from growing unbounded if the network is very bad.
@export_range(2, 30) var jitter_max_buffered_frames: int = 10:
	set(value):
		value = clampi(value, 2, 30)
		if jitter_max_buffered_frames == value:
			return
		jitter_max_buffered_frames = value
		_mark_dirty()

## If true, VoiceTransmitter generates and assigns a correctly
## configured AudioStreamGenerator on every registered player itself (right
## mix_rate, a sane buffer_length) instead of requiring you to hand-configure
## each one and keep it in sync whenever the config changes at runtime.
## Not super useful, might remove later idk.
@export var auto_configure_players: bool = true:
	set(value):
		if auto_configure_players == value:
			return
		auto_configure_players = value
		_mark_dirty()

var _dirty: bool = false

func _mark_dirty() -> void:
	_dirty = true
	call_deferred("_emit_config_changed")

func _emit_config_changed() -> void:
	_dirty = false
	config_changed.emit()

## Samples-per-channel for one opus frame at the current settings.
func get_frame_size() -> int:
	return int(int(opus_sample_rate) * int(opus_frame_duration_ms) / 1000.0)

## Plain-data snapshot for sending over RPC (Resources don't serialize well
## as RPC arguments — a Dictionary of primitives does).
func to_dict() -> Dictionary:
	return {
		"opus_sample_rate": opus_sample_rate,
		"opus_channels": opus_channels,
		"opus_bitrate": opus_bitrate,
		"opus_complexity": opus_complexity,
		"opus_frame_duration_ms": opus_frame_duration_ms,
		"opus_expected_packet_loss_percent": opus_expected_packet_loss_percent,
		"vad_threshold": vad_threshold,
		"vad_release_threshold": vad_release_threshold,
		"vad_hang_time_sec": vad_hang_time_sec,
		"jitter_target_depth_frames": jitter_target_depth_frames,
		"jitter_max_buffered_frames": jitter_max_buffered_frames,
		"auto_configure_players": auto_configure_players,
	}

## Applies a dict produced by to_dict(). Goes through the normal property
## setters, so values are still validated/clamped and only actually mark the
## resource dirty if something really changed.
func apply_dict(d: Dictionary) -> void:
	if d.has("opus_sample_rate"):
		opus_sample_rate = d["opus_sample_rate"]
	if d.has("opus_channels"):
		opus_channels = d["opus_channels"]
	if d.has("opus_bitrate"):
		opus_bitrate = d["opus_bitrate"]
	if d.has("opus_complexity"):
		opus_complexity = d["opus_complexity"]
	if d.has("opus_frame_duration_ms"):
		opus_frame_duration_ms = d["opus_frame_duration_ms"]
	if d.has("opus_expected_packet_loss_percent"):
		opus_expected_packet_loss_percent = d["opus_expected_packet_loss_percent"]
	if d.has("vad_threshold"):
		vad_threshold = d["vad_threshold"]
	if d.has("vad_release_threshold"):
		vad_release_threshold = d["vad_release_threshold"]
	if d.has("vad_hang_time_sec"):
		vad_hang_time_sec = d["vad_hang_time_sec"]
	if d.has("jitter_target_depth_frames"):
		jitter_target_depth_frames = d["jitter_target_depth_frames"]
	if d.has("jitter_max_buffered_frames"):
		jitter_max_buffered_frames = d["jitter_max_buffered_frames"]
	if d.has("auto_configure_players"):
		auto_configure_players = d["auto_configure_players"]
