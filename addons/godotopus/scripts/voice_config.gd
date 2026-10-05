extends Resource
class_name VoiceConfig

## Central configuration for a [VoiceTransmitter].
##
## Every property validates/clamps on assignment and marks the resource
## dirty on any actual change. [VoiceTransmitter] polls [method is_dirty]
## once per [code]_process()[/code] and reinitializes if true.[br]
## If the transmitter has multiplayer authority,
## it will also rpc the new settings out via [method to_dict]/[method apply_dict] so
## every other peer's matching component reconfigures to match (e.g. you can
## drop bitrate at runtime in response to a peer's connection degrading, and
## everyone listening to that peer picks it up automatically).
##
## [b]Fields NOT covered here[/b], because they are per-node behavior rather than
## shared codec/network settings, live directly on [VoiceTransmitter] instead:
## [code]capture_bus_name[/code], [code]process_while_paused[/code], [code]players[/code].

signal config_changed

@export_group("Encoding")

## The sample rate the input signal is resampled to before encoding.
## Opus can encode at [code]8000[/code], [code]12000[/code], [code]16000[/code], [code]24000[/code], or [code]48000[/code] Hz.[br]
## It is recommended to always use [code]48000[/code]. Changing this does [b]not[/b] affect bitrate or reliability in any meaningful way.
## Opus automatically selects its internal bandwidth class ([[color=red]Narrow Band[/color]|[color=yellow]Wide Band[/color]|[color=green]Full Band[/color]) based on
## [member opus_bitrate], regardless of this setting. This option exists only for edge cases
@export_enum("8000:8000", "12000:12000", "16000:16000", "24000:24000", "48000:48000")
var opus_sample_rate: int = 48000:
	set(value):
		if opus_sample_rate == value:
			return
		opus_sample_rate = value
		_mark_dirty()

## [code]1[/code] or [code]2[/code] channels. [code]1[/code] is mono, [code]2[/code] is stereo. Stereo is 2x the bandwidth and almost never used for voice chat anyways.[br]
## (I will not be extensively testing stereo audio, so if it breaks don't hold me accountable!)
@export_range(1, 2) var opus_channels: int = 1:
	set(value):
		value = clampi(value, 1, 2)
		if opus_channels == value:
			return
		opus_channels = value
		_mark_dirty()

## Bitrate in bits per second to encode the audio at. Lower will save bandwidth, but reduce quality.[br]
## This is the [b]primary control[/b] for audio quality and bandwidth tradeoff. Opus automatically adapts
## its internal encoding strategy based on this setting:[br]
## [b]Quality levels (mono speech, 20ms frames):[/b][br]
## [code]6 Kb/s[/code] = Fair, intelligible (narrow-band)[br]
## [code]9-10 Kb/s[/code] = Telephone quality (wide-band)[br]
## [code]12 Kb/s[/code] = Medium bandwidth, better than telephone (super-wideband)[br]
## [code]16 Kb/s[/code] = Wideband speech quality[br]
## [code]24 Kb/s[/code] = Near transparent speech[br]
## [code]32+ Kb/s[/code] = Essentially transparent speech, decent stereo[br]
## [color=yellow]Note:[/color] There are a lot of variables that affect these numbers, ranges may be inaccurate. The above numbers are just rough averages.
@export_range(2000, 128000, 1000) var opus_bitrate: int = 12000:
	set(value):
		value = clampi(value, 6000, 128000)
		if opus_bitrate == value:
			return
		opus_bitrate = value
		_mark_dirty()

## Encoding quality for the Opus encoder.[br]
## [code]0[/code]-[code]10[/code], where [code]10[/code] is the most CPU-intensive and highest quality.[br]
## Since we only have one encode stream per user, this should probably be kept at the max of [code]10[/code] unless
## you have good reason to lower it.
@export_range(0, 10) var opus_complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if opus_complexity == value:
			return
		opus_complexity = value
		_mark_dirty()

## This roughly equates to the number of RPC calls per second.[br]
## Lower values send more smaller packets, whereas higher values send fewer larger packets.[br]
## Since RPC calls have a non-trivial amount of overhead, lower values will use more bandwidth,
## but have lower latency and safer packet loss recovery.[br]
## Think like 1 car crash vs 1 train derailment.
@export_enum("5:5", "10:10", "20:20", "40:40", "60:60") var opus_frame_duration_ms: int = 20:
	set(value):
		if opus_frame_duration_ms == value:
			return
		opus_frame_duration_ms = value
		_mark_dirty()

@export_group("Silence & Loss Recovery")

## Lets Opus's own RNN-based voice activity detector decide when you're
## actually talking, and skip transmitting when you're not (DTX -
## Discontinuous Transmission). This replaces what used to be a hand-tuned
## amplitude gate here -- Opus's detector is trained on real speech/noise data
## and is simply better at this than an RMS threshold could ever be, so there's
## nothing left here for you to tune.[br]
##
## [color=yellow]Important:[/color] DTX only engages once Opus's internal SILK VAD judges the
## signal to be genuinely near-silent for a sustained stretch. A raw,
## ungated mic feeding in continuous (even quiet) room/self-noise may never
## cross that threshold, so [code]encode()[/code] keeps producing small "real" packets
## instead of switching to DTX comfort noise -- that's correct, expected
## Opus behavior, not a bug here. If you want DTX to engage more readily,
## pair this with an upstream noise gate/suppressor (OS-level, driver-level,
## or an [code]AudioEffect[/code] on your capture bus) rather than expecting Opus to
## treat a noisy-but-quiet mic signal as silence on its own.
@export var use_dtx: bool = true:
	set(value):
		if use_dtx == value:
			return
		use_dtx = value
		_mark_dirty()

## Tunes the Opus encoder to add redundancy to each packet to help recover from packet loss.[br]
## Higher values will increase bandwidth usage, but help keep things smooth on lossy connections like WiFi.[br]
## Set to the amount of packets you roughly expect to lose on average.
@export_range(0, 100) var opus_expected_packet_loss_percent: int = 10:
	set(value):
		value = clampi(value, 0, 100)
		if opus_expected_packet_loss_percent == value:
			return
		opus_expected_packet_loss_percent = value
		_mark_dirty()

## Deep REDundancy (Opus 1.5+): embeds a compressed history of recent audio in
## every packet, so a receiver who's missed several packets in a row can
## recover far more of them than plain FEC (which only ever recovers the one
## previous frame). [code]0[/code] disables it. Costs some bitrate the higher you go, and
## only actually does anything if this addon was built with [code]scons dred=yes[/code]
## (see README) -- it's a harmless no-op otherwise, so it's safe to leave
## nonzero either way.
@export_range(0, 1000, 10) var dred_duration_ms: int = 0:
	set(value):
		value = clampi(value, 0, 1000)
		if dred_duration_ms == value:
			return
		dred_duration_ms = value
		_mark_dirty()

@export_group("Decoding & Playback Quality")

## Decode-side complexity, separate from [member opus_complexity] (which is encode-side).[br]
## [code]0[/code]-[code]10[/code]. Besides CPU cost, this also gates libopus 1.5+'s DNN-based packet loss
## concealment (needs >=5) and OSCE speech enhancement (needs >=4-7 depending
## on feature) when this addon was built with DNN support -- higher here means
## meaningfully better-sounding recovery from packet loss and bandwidth
## extension, not just CPU cost, so keep it high unless you're CPU constrained.
@export_range(0, 10) var opus_decoder_complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if opus_decoder_complexity == value:
			return
		opus_decoder_complexity = value
		_mark_dirty()

## OSCE blind Bandwidth Extension (Opus 1.6+): reconstructs full audio
## bandwidth from a narrower encoded signal for free (no extra bits sent).[br]

## [member opus_decoder_complexity] >= [code]4[/code] -- a harmless no-op otherwise, so it's safe
## to just leave this on.[br]
##
## Bandwidth extension only activates at moderate bitrates where Opus
## already encodes a reasonable passband (typically 16 Kb/s+). At very low bitrates (below ~12 Kb/s),
## Opus prioritizes speech intelligibility over bandwidth, so extension may have limited effect.
## At typical VoIP bitrates (16+ Kb/s), bandwidth extension meaningfully improves the output.[br]
## [br]
## Opus chooses its encoding strategy automatically based on [member opus_bitrate], [b]regardless[/b] of what
## [member opus_sample_rate] either side is configured with. There's no manual precondition for you to
## configure to make this work; it either applies to a given packet or it doesn't, packet to packet.[br]
## [color=yellow]Note:[/color] Requires the neural network support build.
@export var enable_bandwidth_extension: bool = true:
	set(value):
		if enable_bandwidth_extension == value:
			return
		enable_bandwidth_extension = value
		_mark_dirty()

## Fade-in duration (seconds) applied to the very start of a burst of
## incoming audio, right after a silence gap (real or just the other end not
## talking). Masks the tiny click/pop that can otherwise happen when
## playback resumes mid-waveform. Keep this small -- it is a declick, not a
## real fade -- a few milliseconds is plenty.
@export_range(0.0, 0.1, 0.005) var fade_in_sec: float = 0.01:
	set(value):
		value = clampf(value, 0.0, 0.1)
		if fade_in_sec == value:
			return
		fade_in_sec = value
		_mark_dirty()

## If [code]true[/code], [VoiceTransmitter] generates and assigns a correctly
## configured [AudioStreamGenerator] on every registered player itself (right
## [code]mix_rate[/code], a sane [code]buffer_length[/code]) instead of requiring you to hand-configure
## each one and keep it in sync whenever the config changes at runtime.[br]
## Not super useful, might remove later idk.
@export var auto_configure_players: bool = true:
	set(value):
		if auto_configure_players == value:
			return
		auto_configure_players = value
		_mark_dirty()

@export_group("Jitter Buffer")

## How many frames of audio the jitter buffer tries to keep in itself in case of network jitter.[br]
## Lower values will reduce latency, but increase the chance of audio dropouts.
@export_range(1, 10) var jitter_target_depth_frames: int = 3:
	set(value):
		value = clampi(value, 1, 10)
		if jitter_target_depth_frames == value:
			return
		jitter_target_depth_frames = value
		_mark_dirty()

## How many frames of audio the jitter buffer will allow to be buffered before it resyncs to
## the most recent arrivals and discards the rest outright. This is the safety valve that keeps
## a stall (yours or the sender's) from turning into a growing latency spike once things resume --
## see [JitterBuffer] for details.
@export_range(2, 30) var jitter_max_buffered_frames: int = 10:
	set(value):
		value = clampi(value, 2, 30)
		if jitter_max_buffered_frames == value:
			return
		jitter_max_buffered_frames = value
		_mark_dirty()

## How long (in seconds) to wait without receiving any packet from a remote
## peer before considering their stream [code]ended[/code] (purely presentational, e.g.
## for a "is talking" UI indicator). Since silence no longer transmits
## anything at all (see [member use_dtx]), this can't be inferred from the packets
## themselves and needs a wall-clock timeout instead.
@export_range(0.1, 3.0, 0.05) var remote_silence_timeout_sec: float = 0.5:
	set(value):
		value = clampf(value, 0.1, 3.0)
		if remote_silence_timeout_sec == value:
			return
		remote_silence_timeout_sec = value
		_mark_dirty()

@export_group("Diagnostics")

## Periodically prints a breakdown of where audio is buffered end to end
## (capture backlog, jitter buffer depth, playback buffer fill) to help
## diagnose latency build-up, plus notable one-off events (backlog trims,
## sequence resyncs, loss-concealment gaps). Purely a local diagnostic
## switch, so it is intentionally [b]not[/b] synced to other peers via
## [method to_dict]/[method apply_dict].
@export var debug_log_latency: bool = false

var _dirty: bool = false


func _mark_dirty() -> void:
	_dirty = true
	call_deferred("_emit_config_changed")


func _emit_config_changed() -> void:
	_dirty = false
	config_changed.emit()


## Samples-per-channel for one opus frame at the current encode settings.
func get_frame_size() -> int:
	return int(int(opus_sample_rate) * int(opus_frame_duration_ms) / 1000.0)


## Plain-data snapshot for sending over RPC ([Resource]s don't serialize well
## as RPC arguments -- a [Dictionary] of primitives does).
func to_dict() -> Dictionary:
	return {
		"opus_sample_rate": opus_sample_rate,
		"opus_channels": opus_channels,
		"opus_bitrate": opus_bitrate,
		"opus_complexity": opus_complexity,
		"opus_frame_duration_ms": opus_frame_duration_ms,
		"use_dtx": use_dtx,
		"opus_expected_packet_loss_percent": opus_expected_packet_loss_percent,
		"dred_duration_ms": dred_duration_ms,
		"opus_decoder_complexity": opus_decoder_complexity,
		"enable_bandwidth_extension": enable_bandwidth_extension,
		"fade_in_sec": fade_in_sec,
		"auto_configure_players": auto_configure_players,
		"jitter_target_depth_frames": jitter_target_depth_frames,
		"jitter_max_buffered_frames": jitter_max_buffered_frames,
		"remote_silence_timeout_sec": remote_silence_timeout_sec,
	}


## Applies a dict produced by [method to_dict]. Goes through the normal property
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
	if d.has("use_dtx"):
		use_dtx = d["use_dtx"]
	if d.has("opus_expected_packet_loss_percent"):
		opus_expected_packet_loss_percent = d["opus_expected_packet_loss_percent"]
	if d.has("dred_duration_ms"):
		dred_duration_ms = d["dred_duration_ms"]
	if d.has("opus_decoder_complexity"):
		opus_decoder_complexity = d["opus_decoder_complexity"]
	if d.has("enable_bandwidth_extension"):
		enable_bandwidth_extension = d["enable_bandwidth_extension"]
	if d.has("fade_in_sec"):
		fade_in_sec = d["fade_in_sec"]
	if d.has("auto_configure_players"):
		auto_configure_players = d["auto_configure_players"]
	if d.has("jitter_target_depth_frames"):
		jitter_target_depth_frames = d["jitter_target_depth_frames"]
	if d.has("jitter_max_buffered_frames"):
		jitter_max_buffered_frames = d["jitter_max_buffered_frames"]
	if d.has("remote_silence_timeout_sec"):
		remote_silence_timeout_sec = d["remote_silence_timeout_sec"]
