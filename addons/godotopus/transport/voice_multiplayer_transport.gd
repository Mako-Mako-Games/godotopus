class_name VoiceMultiplayerTransport
extends VoiceTransport
## Sends voice with RPCs over the scene's [MultiplayerAPI].
##
## Every peer needs this node at the same path, like any node that uses
## RPCs. Packets go out unreliable and ordered on their own [member channel],
## so voice never waits behind reliable game traffic. Each packet arrives
## tagged with the id of the peer that sent it; [VoiceReceiver.peer_id]
## filters on that, so a peer can't speak as someone else.

## Transfer channel for voice packets. Keep it free of other traffic.
@export_range(0, 255) var channel: int = 3:
	set(value):
		channel = clampi(value, 0, 255)
		_configure_rpc()

## Peers to send to. Empty sends to every connected peer.
var target_peers := PackedInt32Array()


func _ready() -> void:
	_configure_rpc()


func send(packet: PackedByteArray, sequence: int) -> void:
	var peer := multiplayer.multiplayer_peer
	if peer == null or peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED:
		return
	if target_peers.is_empty():
		_receive_packet.rpc(packet, sequence)
		return
	var own_id := multiplayer.get_unique_id()
	for id in target_peers:
		if id != own_id:
			_receive_packet.rpc_id(id, packet, sequence)


func _configure_rpc() -> void:
	rpc_config(
		&"_receive_packet",
		{
			"rpc_mode": MultiplayerAPI.RPC_MODE_ANY_PEER,
			"transfer_mode": MultiplayerPeer.TRANSFER_MODE_UNRELIABLE_ORDERED,
			"call_local": false,
			"channel": channel,
		}
	)


func _receive_packet(packet: PackedByteArray, sequence: int) -> void:
	packet_received.emit(multiplayer.get_remote_sender_id(), packet, sequence)
