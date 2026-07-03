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

## Lets Opus's own RNN-based voice activity detector decide when you're
## actually talking, and skip transmitting when you're not (DTX -
## Discontinuous Transmission). This replaces what used to be a hand-tuned
## amplitude gate here — Opus's detector is trained on real speech/noise data
## and is simply better at this than an RMS threshold could ever be, so there's
## nothing left here for you to tune. Leave this on unless you have a very
## specific reason not to (e.g. you always want to transmit, silence included).
@export var use_dtx: bool = true:
	set(value):
		if use_dtx == value:
			return
		use_dtx = value
		_mark_dirty()

## Deep REDundancy (Opus 1.5+): embeds a compressed history of recent audio in
## every packet, so a receiver who's missed several packets in a row can
## recover far more of them than plain FEC (which only ever recovers the one
## previous frame). 0 disables it. Costs some bitrate the higher you go, and
## only actually does anything if this addon was built with `scons dred=yes`
## (see README) — it's a harmless no-op otherwise, so it's safe to leave
## nonzero either way.
@export_range(0, 1000, 10) var dred_duration_ms: int = 0:
	set(value):
		value = clampi(value, 0, 1000)
		if dred_duration_ms == value:
			return
		dred_duration_ms = value
		_mark_dirty()

## Decode-side complexity, separate from opus_complexity (which is encode-side).
## 0-10. Besides CPU cost, this also gates libopus 1.5+'s DNN-based packet loss
## concealment (needs >=5) and OSCE speech enhancement (needs >=6, or >=7 for
## the higher-quality variant) when this addon was built with DNN support —
## higher here means meaningfully better-sounding recovery from packet loss,
## not just CPU cost, so keep it high unless you're CPU constrained.
@export_range(0, 10) var opus_decoder_complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if opus_decoder_complexity == value:
			return
		opus_decoder_complexity = value
		_mark_dirty()

## OSCE blind Bandwidth Extension (Opus 1.6+): reconstructs full audio
## bandwidth from a narrower encoded signal for free (no extra bits sent).
## Only kicks in under specific conditions (decoding at 48kHz from a
## wideband-only SILK signal) and is a harmless no-op otherwise (including on
## builds without DNN support), so it's safe to just leave on.
@export var enable_bandwidth_extension: bool = true:
	set(value):
		if enable_bandwidth_extension == value:
			return
		enable_bandwidth_extension = value
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

## How long (in seconds) to wait without receiving any packet from a remote
## peer before considering their stream_ended (purely presentational, e.g.
## for a "is talking" UI indicator). Since silence no longer transmits
## anything at all (see use_dtx), this can't be inferred from the packets
## themselves and needs a wall-clock timeout instead.
@export_range(0.1, 3.0, 0.05) var remote_silence_timeout_sec: float = 0.5:
	set(value):
		value = clampf(value, 0.1, 3.0)
		if remote_silence_timeout_sec == value:
			return
		remote_silence_timeout_sec = value
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
		"use_dtx": use_dtx,
		"dred_duration_ms": dred_duration_ms,
		"opus_decoder_complexity": opus_decoder_complexity,
		"enable_bandwidth_extension": enable_bandwidth_extension,
		"jitter_target_depth_frames": jitter_target_depth_frames,
		"jitter_max_buffered_frames": jitter_max_buffered_frames,
		"remote_silence_timeout_sec": remote_silence_timeout_sec,
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
	if d.has("use_dtx"):
		use_dtx = d["use_dtx"]
	if d.has("dred_duration_ms"):
		dred_duration_ms = d["dred_duration_ms"]
	if d.has("opus_decoder_complexity"):
		opus_decoder_complexity = d["opus_decoder_complexity"]
	if d.has("enable_bandwidth_extension"):
		enable_bandwidth_extension = d["enable_bandwidth_extension"]
	if d.has("jitter_target_depth_frames"):
		jitter_target_depth_frames = d["jitter_target_depth_frames"]
	if d.has("jitter_max_buffered_frames"):
		jitter_max_buffered_frames = d["jitter_max_buffered_frames"]
	if d.has("remote_silence_timeout_sec"):
		remote_silence_timeout_sec = d["remote_silence_timeout_sec"]
	if d.has("auto_configure_players"):
		auto_configure_players = d["auto_configure_players"]
