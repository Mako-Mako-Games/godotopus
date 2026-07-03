extends Node
class_name VoiceTransmitter

signal stream_started(stream_id: int)
signal stream_ended(stream_id: int)
signal local_amplitude_changed(amplitude: float)
signal remote_amplitude_changed(amplitude: float)


## If false, disables all capture and transmission, and ignores incoming streams.
## Mostly useful for debugging, consider muting audio players instead for actual gameplay.
@export var enabled: bool = true
## You get what this is
@export var config: VoiceConfig
## Name of the audio bus to capture audio from. If empty, uses direct audio input from the `AudioServer`
## which may fix issues in some cases with accumulating audio latency.
@export var capture_bus_name: String = "VoiceCapture"


@export var players: Array[Node]:
	set(value):
		players = value
		for player in players.duplicate():
			if not _is_valid_audio_player(player):
				push_warning("VoiceTransmitter: player '%s' is not a usable audio player (needs a `stream` property and get_stream_playback())" % player.name)
				players.erase(player)
			else:
				_setup_player(player)

var _vad: VoiceActivityDetector
var _encoder: GodotOpusEncoder
var _decoder: GodotOpusDecoder
var _jitter_buffer: JitterBuffer
var _capture_resampler: GodotOpusResampler # only created if mic rate != opus rate


var _output_resamplers: Dictionary = {}

var _capture_accum: PackedFloat32Array = PackedFloat32Array()
var _was_speaking: bool = false
var _send_stream_id: int = 0
var _send_sequence: int = 0

var _recv_stream_id: int = -1

var _capture_effect: AudioEffectCapture
var _capture_bus_index: int = -1


# TODO: Make this a config value in the VoiceConfig
const FADE_IN_SEC := 0.01
var _fade_in_total_samples: int = 0
var _fade_in_samples_remaining: int = 0

const SEQ_MODULO := 65536

func _ready() -> void:
	if config == null:
		config = VoiceConfig.new()
		push_warning("VoiceTransmitter: no VoiceConfig assigned, using defaults.")
	else:
		config = config.duplicate()
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

@rpc("any_peer", "call_remote", "reliable")
func _request_config_sync() -> void:
	if not is_multiplayer_authority() or not multiplayer.has_multiplayer_peer():
		return
	_receive_config_update.rpc_id(multiplayer.get_remote_sender_id(), config.to_dict())

func _process(delta: float) -> void:
	if not enabled:
		return
	if config.is_dirty():
		_reinitialize()

	if is_multiplayer_authority():
		_transmit_tick(delta)

	_jitter_buffer.process_tick()
	if _jitter_buffer.has_ready_frames():
		var pcm := _flatten(_jitter_buffer.pull_ready_frames())
		pcm = _apply_fade_in(pcm)
		_distribute_to_consumers(pcm)

func _reinitialize() -> void:
	if _was_speaking:
		_announce_stream_ended()
	_was_speaking = false
	_capture_accum = PackedFloat32Array()

	_vad = VoiceActivityDetector.new()
	_vad.configure(config.vad_threshold, config.vad_release_threshold, config.vad_hang_time_sec)

	var sample_rate := config.opus_sample_rate

	_encoder = GodotOpusEncoder.new()
	_encoder.initialize(sample_rate, config.opus_channels, config.opus_frame_duration_ms)
	_encoder.set_bitrate(config.opus_bitrate)
	_encoder.set_complexity(config.opus_complexity)
	_encoder.set_expected_packet_loss(config.opus_expected_packet_loss_percent)
	_encoder.set_inband_fec(true)
	_encoder.set_signal_voice(true)

	_decoder = GodotOpusDecoder.new()
	_decoder.initialize(sample_rate, config.opus_channels, config.opus_frame_duration_ms)

	_jitter_buffer = JitterBuffer.new()
	_jitter_buffer.configure(_decoder, config.jitter_target_depth_frames, config.jitter_max_buffered_frames)
	_recv_stream_id = -1

	_output_resamplers.clear()

	_capture_resampler = null
	var mix_rate := int(AudioServer.get_mix_rate())
	if mix_rate != sample_rate:
		_capture_resampler = GodotOpusResampler.new()
		_capture_resampler.initialize(mix_rate, sample_rate, config.opus_channels)

	for player in players:
		if is_instance_valid(player):
			_configure_player_stream(player)

	config.mark_clean()

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

	var stereo: PackedVector2Array = _get_input_frames(available)
	var pcm : PackedFloat32Array = _stereo_to_pcm(stereo)

	if _capture_resampler:
		pcm = _capture_resampler.resample(pcm)
	_capture_accum.append_array(pcm)

	var frame_samples := config.get_frame_size() * config.opus_channels
	while _capture_accum.size() >= frame_samples:
		var frame := _capture_accum.slice(0, frame_samples)
		_capture_accum = _capture_accum.slice(frame_samples)
		_process_capture_frame(frame, delta)

func _process_capture_frame(frame: PackedFloat32Array, delta: float) -> void:
	var speaking := _vad.process(frame, delta)
	local_amplitude_changed.emit(_vad.get_last_amplitude())

	if speaking and not _was_speaking:
		_send_stream_id = _wrap(_send_stream_id + 1)
		_send_sequence = 0
		_encoder.reset()
		_on_remote_stream_started.rpc(_send_stream_id)
		stream_started.emit(_send_stream_id)

	if speaking:
		var opus_bytes := _encoder.encode(frame)
		if opus_bytes.size() > 0:
			_receive_voice_packet.rpc(opus_bytes, _send_sequence, _send_stream_id, _vad.get_last_amplitude())
			_send_sequence = _wrap(_send_sequence + 1)
	elif _was_speaking:
		_announce_stream_ended()

	_was_speaking = speaking

func _announce_stream_ended() -> void:
	_on_remote_stream_ended.rpc(_send_stream_id)
	stream_ended.emit(_send_stream_id)


@rpc("authority", "call_remote", "unreliable_ordered")
func _receive_voice_packet(opus_bytes: PackedByteArray, sequence: int, stream_id: int, amplitude: float) -> void:
	if stream_id != _recv_stream_id:
		_begin_new_incoming_stream(stream_id)
	remote_amplitude_changed.emit(amplitude)
	_jitter_buffer.insert(opus_bytes, sequence)

@rpc("authority", "call_remote", "reliable")
func _on_remote_stream_started(stream_id: int) -> void:
	_begin_new_incoming_stream(stream_id)
	stream_started.emit(stream_id)

@rpc("authority", "call_remote", "reliable")
func _on_remote_stream_ended(stream_id: int) -> void:
	stream_ended.emit(stream_id)

func _begin_new_incoming_stream(stream_id: int) -> void:
	_recv_stream_id = stream_id
	_jitter_buffer.reset()
	_clear_consumer_buffers()
	_fade_in_total_samples = max(1, int(config.opus_sample_rate * FADE_IN_SEC))
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
	var desired_rate := float(config.opus_sample_rate)
	var current_gen := node.stream as AudioStreamGenerator
	if current_gen and is_equal_approx(current_gen.mix_rate, desired_rate):
		return
	var gen := AudioStreamGenerator.new()
	gen.mix_rate = desired_rate
	# Comfortably covers the jitter buffer's worst-case backlog with headroom,
	# while staying well under the 0.5s default that would otherwise stack
	# extra latency on top of the jitter buffer's own.
	gen.buffer_length = maxf(0.1, (config.jitter_max_buffered_frames * config.opus_frame_duration_ms / 1000.0) * 2.0)
	node.stream = gen


func _clear_consumer_buffers() -> void:
	if not config.auto_configure_players:
		return
	for player in players:
		if not is_instance_valid(player):
			continue
		var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()
		if playback:
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
	if target_rate == config.opus_sample_rate:
		return pcm

	var id := node.get_instance_id()
	var resampler: GodotOpusResampler = _output_resamplers.get(id)
	if resampler == null:
		resampler = GodotOpusResampler.new()
		resampler.initialize(config.opus_sample_rate, target_rate, config.opus_channels)
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
	return is_multiplayer_authority() and _was_speaking

func get_remote_stream_id() -> int:
	return _recv_stream_id
