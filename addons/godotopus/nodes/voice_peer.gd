class_name VoicePeer
extends Node
## Plug-and-play voice chat for one multiplayer peer.
##
## Put one in each player's scene. On the peer with multiplayer authority
## over it, it sends that peer's microphone; on every other peer it plays
## that voice through [member players]. It builds a [VoiceSender] or a
## [VoiceReceiver] plus a [VoiceMultiplayerTransport] as children when it
## enters the tree, so set its multiplayer authority before then (as with
## [MultiplayerSpawner]-spawned scenes) and keep it at the same path on
## every peer.

## The local or remote voice started.
signal speaking_started
## The local or remote voice went quiet.
signal speaking_ended
## Level of the local microphone or the remote voice.
signal level_changed(level: float)

## Used when this peer sends.
@export var encoder_settings: VoiceEncoderSettings
## Used when this peer plays someone else.
@export var playback_settings: VoicePlaybackSettings
## Players for the remote voice. Ignored on the sending peer.
@export var players: Array[Node] = []
## See [member VoiceSender.capture_bus].
@export var capture_bus: StringName = &""
## See [member VoiceMultiplayerTransport.channel].
@export_range(0, 255) var channel: int = 3

## The sender, on the peer with authority; otherwise null.
var sender: VoiceSender
## The receiver, on every other peer; otherwise null.
var receiver: VoiceReceiver
var transport: VoiceMultiplayerTransport


func _ready() -> void:
	transport = VoiceMultiplayerTransport.new()
	transport.name = "Transport"
	transport.channel = channel
	add_child(transport)

	if is_multiplayer_authority():
		sender = VoiceSender.new()
		sender.name = "Sender"
		sender.settings = encoder_settings
		sender.capture_bus = capture_bus
		sender.transport = transport
		_forward_signals(sender)
		add_child(sender)
	else:
		receiver = VoiceReceiver.new()
		receiver.name = "Receiver"
		receiver.settings = playback_settings
		receiver.peer_id = get_multiplayer_authority()
		receiver.players = players
		receiver.transport = transport
		_forward_signals(receiver)
		add_child(receiver)


## True on the peer whose microphone this is.
func is_local() -> bool:
	return sender != null


func is_speaking() -> bool:
	return sender.is_speaking() if sender != null else receiver.is_speaking()


func _forward_signals(node: Node) -> void:
	node.speaking_started.connect(speaking_started.emit)
	node.speaking_ended.connect(speaking_ended.emit)
	node.level_changed.connect(level_changed.emit)
