class_name VoiceReceiver
extends Node
## Plays one remote voice through any number of audio players.
##
## Feed it packets with [method push_packet], or point [member transport] at
## a [VoiceTransport]. It owns a [VoiceStream] and assigns it to every node in
## [member players] ([AudioStreamPlayer], [AudioStreamPlayer2D] or
## [AudioStreamPlayer3D]); they all play the same voice, so one can be dry
## and another on a radio-effect bus. Decoding happens on the audio thread.
##
## One receiver plays one sender. With a shared transport, set
## [member peer_id] so other senders' packets are ignored.

## Audio started arriving after silence.
signal speaking_started
## Nothing arrived for [member VoicePlaybackSettings.speaking_timeout_ms].
signal speaking_ended
## RMS level of the decoded audio, emitted while it changes.
signal level_changed(level: float)

## The playback preset. Assigning one stores a copy; edit that copy (read this
## property back) to change settings at runtime.
@export var settings: VoicePlaybackSettings:
	set = set_settings

## Players that play this voice. They're started automatically.
@export var players: Array[Node] = []:
	set = set_players

## Delivers packets to this receiver. Optional: [method push_packet] works
## without one.
@export var transport: VoiceTransport:
	set = set_transport

## Only packets from this peer are played. 0 accepts any peer.
@export var peer_id: int = 0

var _stream := VoiceStream.new()
var _speaking := false
var _level := 0.0


func _init() -> void:
	process_mode = PROCESS_MODE_ALWAYS
	if settings == null:
		settings = VoicePlaybackSettings.new()


func _ready() -> void:
	for player in players:
		_start(player)


func _process(_delta: float) -> void:
	poll()


## Emits the speaking and level signals for any change since the last call.
## Called every frame; call it yourself if the node isn't processing.
func poll() -> void:
	var speaking := _stream.is_speaking()
	if speaking != _speaking:
		_speaking = speaking
		if speaking:
			speaking_started.emit()
		else:
			speaking_ended.emit()
	var level := _stream.get_level()
	if level != _level:
		_level = level
		level_changed.emit(level)


func set_settings(value: VoicePlaybackSettings) -> void:
	if settings != null and settings.changed.is_connected(_apply_settings):
		settings.changed.disconnect(_apply_settings)
	settings = value.duplicate() if value != null else VoicePlaybackSettings.new()
	settings.changed.connect(_apply_settings)
	_apply_settings()


func set_players(value: Array[Node]) -> void:
	for player in players:
		if is_instance_valid(player) and player not in value and player.stream == _stream:
			player.stop()
			player.stream = null
	players = []
	for player in value:
		add_player(player)


func set_transport(value: VoiceTransport) -> void:
	if transport != null and transport.packet_received.is_connected(_on_packet_received):
		transport.packet_received.disconnect(_on_packet_received)
	transport = value
	if transport != null:
		transport.packet_received.connect(_on_packet_received)


## Plays this voice through [param player] too.
func add_player(player: Node) -> void:
	if not _is_audio_player(player):
		push_error(
			(
				"VoiceReceiver: %s isn't an AudioStreamPlayer, AudioStreamPlayer2D or AudioStreamPlayer3D."
				% player
			)
		)
		return
	if player in players:
		return
	players.append(player)
	player.stream = _stream
	if is_node_ready():
		_start(player)


func remove_player(player: Node) -> void:
	if player not in players:
		return
	players.erase(player)
	if is_instance_valid(player) and player.stream == _stream:
		player.stop()
		player.stream = null


## Queues one received packet. [param sequence] is the sender's number for
## it, 0 to 65535. Safe to call from any thread.
func push_packet(packet: PackedByteArray, sequence: int) -> void:
	_stream.push_packet(packet, sequence)


## Drops buffered audio, e.g. when the sender changes.
func reset() -> void:
	_stream.reset()


## The stream the players play. Assign it to more players yourself, or mix
## it directly in tests.
func get_stream() -> VoiceStream:
	return _stream


func is_speaking() -> bool:
	return _stream.is_speaking()


func get_level() -> float:
	return _stream.get_level()


## Receive-side counters; see [method VoiceStream.get_stats].
func get_stats() -> Dictionary:
	return _stream.get_stats()


func _apply_settings() -> void:
	_stream.target_latency_ms = settings.target_latency_ms
	_stream.max_latency_ms = settings.max_latency_ms
	_stream.max_conceal_ms = settings.max_conceal_ms
	_stream.channels = settings.channels
	_stream.decoder_complexity = settings.decoder_complexity
	_stream.bandwidth_extension = settings.bandwidth_extension
	_stream.speaking_timeout_ms = settings.speaking_timeout_ms


func _on_packet_received(from_peer: int, packet: PackedByteArray, sequence: int) -> void:
	if peer_id == 0 or from_peer == peer_id:
		_stream.push_packet(packet, sequence)


func _start(player: Node) -> void:
	if not is_instance_valid(player):
		return
	if player.is_inside_tree():
		player.play()
	elif not player.tree_entered.is_connected(player.play):
		player.tree_entered.connect(player.play, CONNECT_ONE_SHOT)


static func _is_audio_player(node: Node) -> bool:
	return node is AudioStreamPlayer or node is AudioStreamPlayer2D or node is AudioStreamPlayer3D
