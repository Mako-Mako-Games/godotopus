extends Node
class_name VoiceTransmitter

## stream_id here is a purely local, presentational counter (not something
## synchronized over the network) — see the comments on _local_stream_id /
## _remote_stream_id below for why that's now the safer design.
signal stream_started(stream_id: int)
signal stream_ended(stream_id: int)
signal local_amplitude_changed(amplitude: float)
signal remote_amplitude_changed(amplitude: float)


## If false, disables all capture and transmission, and ignores incoming streams.
## Mostly useful for debugging, consider muting audio players instead for actual gameplay.
@export var enabled: bool = true
## You get what this is
@export var config: VoiceConfig:
	set(value):
		if config == value:
			return
		if config and config.config_changed.is_connected(_reinitialize):
			config.config_changed.disconnect(_reinitialize)
		config = value
		if config and not config.config_changed.is_connected(_reinitialize):
			config.config_changed.connect(_reinitialize)
## Name of the audio bus to capture audio from, via an AudioEffectCapture on
## that bus. Leave this empty (recommended) to instead pull mic input
## directly from the AudioServer, bypassing the bus effect chain entirely.
##
## Known issue: routing capture through an audio bus (AudioEffectCapture) has
## a confirmed Godot engine limitation where its internal buffering can
## accumulate excess latency across engine hitches/SceneTree pauses, in a way
## the direct AudioServer path does not exhibit at all. This is a Godot
## engine issue, not something this addon can work around at the script
## level -- it is the entire reason the direct-capture path exists here.
## Only set this to a bus name if you specifically need to run capture
## through bus effects (e.g. a noise gate/suppressor) before encoding, and
## are OK with the latency tradeoff that comes with it.
@export var capture_bus_name: String = "":
	set(value):
		if capture_bus_name == value:
			return
		capture_bus_name = value
		if not Engine.is_editor_hint() and capture_bus_name != "":
			push_warning("VoiceTransmitter: capture_bus_name is set to '%s'. Routing capture through an audio bus has a confirmed Godot engine issue where latency can build up across engine hitches/pauses. Leave capture_bus_name empty (direct AudioServer capture) unless you specifically need bus effects on the mic signal." % capture_bus_name)

## Keeps this node's own _process() (and therefore the entire capture,
## transmit, receive, and decode pipeline) running even while the SceneTree
## is paused (a local pause/debug/settings menu, for instance). Other peers'
## voice keeps flowing over the network the whole time regardless of
## whether your own game is paused -- RPC dispatch is not gated by
## SceneTree.paused, so packets still arrive and queue up even while
## paused, but nothing decodes or plays any of it until _process() resumes.
## Leaving this off means opening a pause/debug menu produces a real
## multi-second latency spike (or dropped audio) the instant you close it.
## Turn it off only if you deliberately want voice chat to freeze along
## with everything else while paused.
@export var process_while_paused: bool = true:
	set(value):
		process_while_paused = value
		process_mode = PROCESS_MODE_ALWAYS if value else PROCESS_MODE_PAUSABLE


@export var players: Array[Node]:
	set(value):
		players = value
		for player in players.duplicate():
			if not _is_valid_audio_player(player):
				push_warning("VoiceTransmitter: player '%s' is not a usable audio player (needs a `stream` property and get_stream_playback())" % player.name)
				players.erase(player)
			else:
				_setup_player(player)

var _encoder: GodotOpusEncoder
var _decoder: GodotOpusDecoder
var _jitter_buffer: JitterBuffer
var _capture_resampler: GodotOpusResampler # only created if mic rate != opus rate


var _output_resamplers: Dictionary = {}

var _capture_accum: PackedFloat32Array = PackedFloat32Array()

# Continuously incrementing — only advances when a packet is actually sent.
# There's no per-utterance stream ID on the wire anymore (see class comment
# in jitter_buffer.gd): since silence is simply never transmitted, sequence
# numbers naturally stay contiguous across a speech pause with no gap for
# the receiver to misinterpret as packet loss, so there's nothing to reset
# and no separate "stream boundary" concept needed at the protocol level.
var _send_sequence: int = 0
var _local_speaking: bool = false
var _local_stream_id: int = 0 # local-only, just for a unique stream_started id

var _remote_speaking: bool = false
var _remote_stream_id: int = 0
var _time_since_last_packet: float = 999.0

var _debug_log_accum_sec: float = 0.0
const DEBUG_LOG_INTERVAL_SEC := 1.0

var _capture_effect: AudioEffectCapture
var _capture_bus_index: int = -1

var _fade_in_total_samples: int = 0
var _fade_in_samples_remaining: int = 0

const SEQ_MODULO := 65536

# Decoding (and therefore playback) always happens at Opus's max sample
# rate, completely independent of whatever opus_sample_rate each sender
# encodes at -- Opus decode has no problem upsampling any encoded bandwidth
# up to this rate, it is cheap for a single voice stream, and it is a hard
# prerequisite for OSCE bandwidth extension to ever have anything to do
# (see VoiceConfig.enable_bandwidth_extension). There is no config knob for
# this: it is strictly better to always decode at full quality.
const DECODE_SAMPLE_RATE := 48000

func _ready() -> void:
	process_mode = PROCESS_MODE_ALWAYS if process_while_paused else PROCESS_MODE_PAUSABLE
	if config == null:
		config = VoiceConfig.new()
		push_warning("VoiceTransmitter: no VoiceConfig assigned, using defaults.")
	_reinitialize()

	if not is_multiplayer_authority():
		_request_initial_config_sync()

func _request_initial_config_sync() -> void:
	if multiplayer.has_multiplayer_peer():
		_request_config_sync.rpc_id(get_multiplayer_authority())
	else:
		multiplayer.connected_to_server.connect(_on_connected_for_initial_sync, CONNECT_ONE_SHOT)

func _on_connected_for_initial_sync() -> void:
	_request_config_sync.rpc_id(get_multiplayer_authority())

@rpc("any_peer", "call_local", "reliable")
func _request_config_sync() -> void:
	if not is_multiplayer_authority() or not multiplayer.has_multiplayer_peer():
		return
	_receive_config_update.rpc_id(multiplayer.get_remote_sender_id(), config.to_dict())

func _process(delta: float) -> void:
	if not enabled:
		return

	if config.debug_log_latency and delta > 0.1:
		print("[VoiceTransmitter] large frame delta: %.0fms (paused, stalled, or a real engine hitch) -- process_while_paused=%s, tree_paused=%s" % [delta * 1000.0, process_while_paused, get_tree().paused])

	if is_multiplayer_authority():
		_transmit_tick(delta)

	_time_since_last_packet += delta
	if _remote_speaking and _time_since_last_packet > config.remote_silence_timeout_sec:
		_remote_speaking = false
		stream_ended.emit(_remote_stream_id)

	_jitter_buffer.process_tick()
	if _jitter_buffer.has_ready_frames():
		var pcm := _flatten(_jitter_buffer.pull_ready_frames())
		pcm = _apply_fade_in(pcm)
		remote_amplitude_changed.emit(_rms(pcm))
		_distribute_to_consumers(pcm)

	if config.debug_log_latency:
		_debug_log_accum_sec += delta
		if _debug_log_accum_sec >= DEBUG_LOG_INTERVAL_SEC:
			_debug_log_accum_sec = 0.0
			_print_latency_snapshot()

func _reinitialize() -> void:
	_local_speaking = false
	_remote_speaking = false
	_time_since_last_packet = 999.0
	_capture_accum = PackedFloat32Array()

	var sample_rate := config.opus_sample_rate

	_encoder = GodotOpusEncoder.new()
	_encoder.initialize(sample_rate, config.opus_channels, config.opus_frame_duration_ms)
	_encoder.set_bitrate(config.opus_bitrate)
	_encoder.set_complexity(config.opus_complexity)
	_encoder.set_expected_packet_loss(config.opus_expected_packet_loss_percent)
	_encoder.set_inband_fec(true)
	_encoder.set_signal_voice(true)
	_encoder.set_dtx(config.use_dtx)
	_encoder.set_dred_duration_ms(config.dred_duration_ms)

	_decoder = GodotOpusDecoder.new()
	_decoder.initialize(DECODE_SAMPLE_RATE, config.opus_channels, config.opus_frame_duration_ms)
	_decoder.set_complexity(config.opus_decoder_complexity)
	_decoder.set_bandwidth_extension(config.enable_bandwidth_extension)

	_jitter_buffer = JitterBuffer.new()
	_jitter_buffer.configure(_decoder, config.jitter_target_depth_frames, config.jitter_max_buffered_frames, config.debug_log_latency)

	_output_resamplers.clear()

	_capture_resampler = null
	var mix_rate := int(AudioServer.get_mix_rate())
	if mix_rate != sample_rate:
		_capture_resampler = GodotOpusResampler.new()
		_capture_resampler.initialize(mix_rate, sample_rate, config.opus_channels)

	for player in players:
		if is_instance_valid(player):
			_configure_player_stream(player)

	if is_multiplayer_authority() and multiplayer.has_multiplayer_peer():
		_receive_config_update.rpc(config.to_dict())

@rpc("authority", "call_remote", "reliable")
func _receive_config_update(data: Dictionary) -> void:
	config.apply_dict(data)

# ── Transmit side ──────────────────────────────────────────────────────────

func _transmit_tick(delta: float) -> void:
	var available : int = _get_input_frames_available()
	if available <= 0:
		return

	if config.debug_log_latency:
		var mix_rate := AudioServer.get_mix_rate()
		var normal_batch := int(mix_rate * delta) + 1
		if available > normal_batch * 3:
			print("[VoiceTransmitter] large capture read this tick: %d frames (~%.0fms) -- input device/OS may be buffering audio" % [available, float(available) / mix_rate * 1000.0])

	var stereo: PackedVector2Array = _get_input_frames(available)
	var pcm : PackedFloat32Array = _stereo_to_pcm(stereo)

	if _capture_resampler:
		pcm = _capture_resampler.resample(pcm)
	_capture_accum.append_array(pcm)

	var frame_samples := config.get_frame_size() * config.opus_channels

	var max_backlog_samples := frame_samples * max(1, config.jitter_max_buffered_frames)
	if _capture_accum.size() > max_backlog_samples:
		# This process stalled/froze for a while (hitch, breakpoint, loading
		# spike) and the input device kept capturing the whole time. Encoding
		# and sending all of that backlog in one burst would just replay stale
		# audio into every listener's jitter buffer at once, reintroducing the
		# exact latency spike the receive side now guards against. Drop the
		# stale portion and only catch up from the most recent audio.
		if config.debug_log_latency:
			var dropped_ms := float(_capture_accum.size() - frame_samples) / float(config.opus_channels) / float(config.opus_sample_rate) * 1000.0
			print("[VoiceTransmitter] capture backlog overflow: dropping ~%.0fms of stale captured audio" % dropped_ms)
		_capture_accum = _capture_accum.slice(_capture_accum.size() - frame_samples)

	var frames_this_tick := 0
	while _capture_accum.size() >= frame_samples:
		var frame := _capture_accum.slice(0, frame_samples)
		_capture_accum = _capture_accum.slice(frame_samples)
		_process_capture_frame(frame, delta)
		frames_this_tick += 1

	if config.debug_log_latency and frames_this_tick > 1:
		print("[VoiceTransmitter] encoded %d frames in a single tick -- capture is running behind real time" % frames_this_tick)

func _process_capture_frame(frame: PackedFloat32Array, delta: float) -> void:
	# Always hand the frame to Opus. With DTX enabled, its own RNN-based voice
	# activity detector decides whether this is speech worth sending — encode()
	# returns an empty (or tiny comfort-noise) packet for silence, and a normal
	# packet for speech. That's the entire transmit decision; there's no
	# amplitude threshold left for us to get wrong.
	var opus_bytes := _encoder.encode(frame)
	local_amplitude_changed.emit(_rms(frame))

	var speaking := opus_bytes.size() > 0 and not _encoder.is_in_dtx()
	if config.debug_log_latency and speaking != _local_speaking:
		print("[VoiceTransmitter] speaking=%s, opus_bytes=%d, dtx=%s" % [speaking, opus_bytes.size(), _encoder.is_in_dtx()])
	if speaking and not _local_speaking:
		_local_stream_id = _wrap(_local_stream_id + 1)
		stream_started.emit(_local_stream_id)
	elif not speaking and _local_speaking:
		stream_ended.emit(_local_stream_id)
	_local_speaking = speaking

	if opus_bytes.size() > 0:
		_receive_voice_packet.rpc(opus_bytes, _send_sequence)
		_send_sequence = _wrap(_send_sequence + 1)


@rpc("authority", "call_remote", "unreliable_ordered")
func _receive_voice_packet(opus_bytes: PackedByteArray, sequence: int) -> void:
	_time_since_last_packet = 0.0
	if not _remote_speaking:
		_remote_speaking = true
		_remote_stream_id = _wrap(_remote_stream_id + 1)
		stream_started.emit(_remote_stream_id)
		_on_remote_activity_resumed()
	_jitter_buffer.insert(opus_bytes, sequence)

## Called the moment packets start arriving again after a silence gap (real
## or just the other end not talking). Doesn't touch the jitter buffer/decoder
## state at all — see jitter_buffer.gd's reset() comment for why that's no
## longer necessary here — just clears out any stale buffered silence on the
## playback side and re-primes the fade-in so resuming audio doesn't click.
func _on_remote_activity_resumed() -> void:
	_clear_consumer_buffers()
	_fade_in_total_samples = max(1, int(DECODE_SAMPLE_RATE * config.fade_in_sec))
	_fade_in_samples_remaining = _fade_in_total_samples

func _apply_fade_in(pcm: PackedFloat32Array) -> PackedFloat32Array:
	if _fade_in_samples_remaining <= 0 or pcm.is_empty():
		return pcm
	var channels := config.opus_channels
	var frame_count := pcm.size() / channels
	var out := pcm.duplicate()
	for i in range(frame_count):
		if _fade_in_samples_remaining <= 0:
			break
		var ramp := 1.0 - float(_fade_in_samples_remaining) / float(_fade_in_total_samples)
		for c in range(channels):
			out[i * channels + c] *= ramp
		_fade_in_samples_remaining -= 1
	return out

# ── Consumers ──────────────────────────────────────────────────────────────

func register_player(node: Node, key: String = "") -> void:
	if not _is_valid_audio_player(node):
		push_error("VoiceTransmitter: '%s' is not a usable audio player (needs a `stream` property and get_stream_playback())" % node.name)
		return
	players.append(node)
	_setup_player(node)

## Accepts a player and removes it from the `players` array.
func unregister_player(player) -> void:
	if is_instance_valid(player):
		_output_resamplers.erase(player.get_instance_id())
	players.erase(player)

func _setup_player(node: Node) -> void:
	_configure_player_stream(node)

func _configure_player_stream(node: Node) -> void:
	if not config.auto_configure_players:
		return
	var desired_rate := float(DECODE_SAMPLE_RATE)
	var current_gen := node.stream as AudioStreamGenerator
	if current_gen and is_equal_approx(current_gen.mix_rate, desired_rate):
		return

	var was_playing : bool = node.has_method("is_playing") and node.is_playing()

	var gen := AudioStreamGenerator.new()
	gen.mix_rate = desired_rate
	gen.buffer_length = maxf(0.1, (config.jitter_max_buffered_frames * config.opus_frame_duration_ms / 1000.0) * 2.0)
	node.stream = gen

	if was_playing:
		node.play()


func _clear_consumer_buffers() -> void:
	if not config.auto_configure_players:
		return
	for player in players:
		if not is_instance_valid(player):
			continue
		var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()
		if playback and not playback.is_playing():
			playback.clear_buffer()

func _distribute_to_consumers(pcm: PackedFloat32Array) -> void:
	if pcm.is_empty() or players.is_empty():
		return

	for p in players:
		if not is_instance_valid(p):
			unregister_player(p)
			continue
		var playback: AudioStreamGeneratorPlayback = p.get_stream_playback()
		if playback == null:
			continue
		var space := playback.get_frames_available()
		if space <= 0:
			continue

		var player_pcm := _resample_for_player(p, pcm)
		var stereo := _pcm_to_stereo(player_pcm)
		var to_push := stereo
		if stereo.size() > space:
			# Only under sustained overload. Push what fits; the remainder is
			# simply not played for this one consumer this tick.
			to_push = stereo.slice(0, space)
		playback.push_buffer(to_push)

func _resample_for_player(node: Node, pcm: PackedFloat32Array) -> PackedFloat32Array:
	if config.auto_configure_players:
		return pcm
	var gen := node.stream as AudioStreamGenerator
	if gen == null:
		return pcm
	var target_rate := int(gen.mix_rate)
	if target_rate == DECODE_SAMPLE_RATE:
		return pcm

	var id := node.get_instance_id()
	var resampler: GodotOpusResampler = _output_resamplers.get(id)
	if resampler == null:
		resampler = GodotOpusResampler.new()
		resampler.initialize(DECODE_SAMPLE_RATE, target_rate, config.opus_channels)
		_output_resamplers[id] = resampler
	return resampler.resample(pcm)

# ── Helpers ────────────────────────────────────────────────────────────────

func _is_valid_audio_player(node: Node) -> bool:
	return is_instance_valid(node) and ("stream" in node) and node.has_method("get_stream_playback") and node.has_method("play")

func _stereo_to_pcm(stereo: PackedVector2Array) -> PackedFloat32Array:
	var pcm := PackedFloat32Array()
	if config.opus_channels == 1:
		pcm.resize(stereo.size())
		for i in range(stereo.size()):
			pcm[i] = (stereo[i].x + stereo[i].y) * 0.5
	else:
		pcm.resize(stereo.size() * 2)
		for i in range(stereo.size()):
			pcm[i * 2] = stereo[i].x
			pcm[i * 2 + 1] = stereo[i].y
	return pcm

func _pcm_to_stereo(pcm: PackedFloat32Array) -> PackedVector2Array:
	var frame_count : int = pcm.size() / config.opus_channels
	var stereo := PackedVector2Array()
	stereo.resize(frame_count)
	if config.opus_channels == 1:
		for i in range(frame_count):
			stereo[i] = Vector2(pcm[i], pcm[i])
	else:
		for i in range(frame_count):
			stereo[i] = Vector2(pcm[i * 2], pcm[i * 2 + 1])
	return stereo

func _flatten(frames: Array[PackedFloat32Array]) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	for f in frames:
		out.append_array(f)
	return out

func _wrap(v: int) -> int:
	return ((v % SEQ_MODULO) + SEQ_MODULO) % SEQ_MODULO

## Cheap loudness meter for UI (mic level bars, "who's talking" glow, etc).
## This is NOT a speech/silence decision — that's entirely Opus's job now
## (see use_dtx) — just a level readout computed straight from PCM.
func _rms(pcm: PackedFloat32Array) -> float:
	if pcm.is_empty():
		return 0.0
	var sum_sq := 0.0
	for sample in pcm:
		sum_sq += sample * sample
	return sqrt(sum_sq / pcm.size())

## Diagnostic only (VoiceConfig.debug_log_latency). Breaks down how much
## audio is currently sitting at each stage of the pipeline, in milliseconds,
## so a latency build-up can be traced to its actual source instead of
## guessed at: mic capture accumulator, jitter buffer (both the not-yet-
## decoded and decoded-but-not-yet-pulled queues), and each consumer's
## AudioStreamGeneratorPlayback ring buffer fill level.
func _print_latency_snapshot() -> void:
	var capture_ms := 0.0
	if config.opus_channels > 0 and config.opus_sample_rate > 0:
		capture_ms = float(_capture_accum.size()) / float(config.opus_channels) / float(config.opus_sample_rate) * 1000.0

	var frame_ms := float(config.opus_frame_duration_ms)
	var raw_ms := _jitter_buffer.get_raw_queue_size() * frame_ms
	var decoded_ms := _jitter_buffer.get_decoded_queue_size() * frame_ms

	var playback_summary := ""
	for player in players:
		if not is_instance_valid(player):
			continue
		var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()
		var gen := player.stream as AudioStreamGenerator
		if playback == null or gen == null:
			continue
		var capacity_frames := int(gen.buffer_length * gen.mix_rate)
		var used_frames := capacity_frames - playback.get_frames_available()
		var used_ms := float(used_frames) / gen.mix_rate * 1000.0
		playback_summary += " %s=%.0fms" % [player.name, used_ms]

	print("[VoiceTransmitter] latency capture=%.0fms jitter_raw=%.0fms jitter_decoded=%.0fms playback:%s last_packet=%.2fs" % [capture_ms, raw_ms, decoded_ms, playback_summary, _time_since_last_packet])

func _get_audio_effect_capture() -> AudioEffectCapture:
	if capture_bus_name == "":
		return null
	var bus_index := AudioServer.get_bus_index(capture_bus_name)
	if bus_index == -1:
		push_error("VoiceTransmitter: capture_bus_name '%s' does not exist" % capture_bus_name)
		return null
	if bus_index != _capture_bus_index:
		_capture_bus_index = bus_index
		var effect_count := AudioServer.get_bus_effect_count(bus_index)
		_capture_effect = null
		for i in effect_count:
			var fx := AudioServer.get_bus_effect(bus_index, i)
			if fx is AudioEffectCapture:
				_capture_effect = fx
				break
		if not _capture_effect:
			push_error("VoiceTransmitter: capture_bus_name '%s' has no AudioEffectCapture" % capture_bus_name)
	return _capture_effect

func _get_input_frames_available() -> int:
	if capture_bus_name != "":
		var capture := _get_audio_effect_capture()
		return capture.get_frames_available() if capture else 0
	else:
		return AudioServer.get_input_frames_available()

func _get_input_frames(count: int) -> PackedVector2Array:
	if capture_bus_name != "":
		var capture := _get_audio_effect_capture()
		return capture.get_buffer(count) if capture else PackedVector2Array()
	else:
		return AudioServer.get_input_frames(count)

# ── Public introspection ───────────────────────────────────────────────────

func is_locally_speaking() -> bool:
	return is_multiplayer_authority() and _local_speaking

## Purely a local, presentational counter (see class comment) — no longer a
## network-synchronized stream ID, just something that changes every time a
## new "utterance" from this remote peer is detected.
func get_remote_stream_id() -> int:
	return _remote_stream_id
