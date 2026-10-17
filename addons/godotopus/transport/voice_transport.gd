class_name VoiceTransport
extends Node
## Carries voice packets between peers.
##
## [VoiceSender] calls [method send] for every packet; a transport delivers
## it to the other peers, where it emits [signal packet_received] for
## [VoiceReceiver]s to pick up. Extend this to send voice over your own
## networking (Steam, a relay server, ...). Packets may be lost, duplicated
## or reordered; receivers handle that. Use an unreliable channel: resending
## late audio only adds latency.
##
## Built-in transports: [VoiceMultiplayerTransport] and
## [VoiceLoopbackTransport].

## A packet arrived from [param peer_id]. [param sequence] is the number the
## sender gave it, 0 to 65535.
@warning_ignore("unused_signal")
signal packet_received(peer_id: int, packet: PackedByteArray, sequence: int)


## Sends one packet to the other peers. Must not block.
func send(_packet: PackedByteArray, _sequence: int) -> void:
	push_error("%s doesn't implement VoiceTransport.send()." % get_script().resource_path)
