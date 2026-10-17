extends GdUnitTestSuite
## VoicePeer and VoiceMultiplayerTransport over a real ENet connection. Two
## branches of the scene tree get their own SceneMultiplayer, one hosting and
## one connected to it on localhost, so both peers live in this process.

const TIMEOUT_MSEC := 5000

var _server: Node
var _client: Node
var _sources: Array[TestSignals.Source] = []


func before_test() -> void:
	_server = auto_free(Node.new())
	_server.name = "Server"
	_client = auto_free(Node.new())
	_client.name = "Client"
	add_child(_server)
	add_child(_client)
	get_tree().set_multiplayer(SceneMultiplayer.new(), _server.get_path())
	get_tree().set_multiplayer(SceneMultiplayer.new(), _client.get_path())


func after_test() -> void:
	for branch in [_client, _server]:
		var api := get_tree().get_multiplayer(branch.get_path())
		if api.multiplayer_peer != null:
			api.multiplayer_peer.close()
		get_tree().set_multiplayer(null, branch.get_path())
	_sources.clear()


## Hosts on the first free port and connects the client. Returns false if
## the connection didn't come up in time.
func _connect() -> bool:
	var host := ENetMultiplayerPeer.new()
	var port := 0
	for candidate in range(24561, 24600):
		if host.create_server(candidate, 2) == OK:
			port = candidate
			break
	if port == 0:
		return false
	var client := ENetMultiplayerPeer.new()
	if client.create_client("127.0.0.1", port) != OK:
		return false
	_server.multiplayer.multiplayer_peer = host
	_client.multiplayer.multiplayer_peer = client
	return await _wait(
		func(): return client.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
	)


func _wait(condition: Callable) -> bool:
	var start := Time.get_ticks_msec()
	while not condition.call():
		if Time.get_ticks_msec() - start > TIMEOUT_MSEC:
			return false
		await get_tree().process_frame
	return true


## Adds a VoicePeer named [param peer_name], owned by [param owner_id], under
## both branches, like a spawned player scene. Returns [server copy, client copy].
func _spawn(peer_name: String, owner_id: int) -> Array[VoicePeer]:
	var copies: Array[VoicePeer] = []
	for branch in [_server, _client]:
		var player := AudioStreamPlayer.new()
		branch.add_child(player)
		var peer := VoicePeer.new()
		peer.name = peer_name
		peer.players = [player]
		peer.set_multiplayer_authority(owner_id)
		branch.add_child(peer)
		copies.append(peer)
	return copies


func _talk(peer: VoicePeer) -> void:
	var source := TestSignals.Source.new(TestSignals.voice_like(), 48000, 2.0)
	_sources.append(source)  # a Callable doesn't keep its object alive
	peer.sender.set_capture_source(source.read, source.rate)


func _assert_hears(listener: VoicePeer, talker_id: int) -> void:
	assert_bool(listener.is_local()).is_false()
	assert_int(listener.receiver.peer_id).is_equal(talker_id)
	var started := [false]
	listener.speaking_started.connect(func(): started[0] = true)
	var heard := await _wait(
		func(): return listener.receiver.get_stats()["packets_received"] >= 20 and started[0]
	)
	var stats := listener.receiver.get_stats()
	assert_bool(heard).override_failure_message("no voice: %s" % stats).is_true()
	assert_int(stats["decode_errors"]).is_equal(0)
	assert_int(stats["resyncs"]).is_equal(0)


func test_server_voice_reaches_the_client() -> void:
	assert_bool(await _connect()).override_failure_message("couldn't connect").is_true()
	var copies := _spawn("Host", 1)
	var talker := copies[0]
	assert_bool(talker.is_local()).is_true()
	assert_object(talker.receiver).is_null()
	_talk(talker)
	await _assert_hears(copies[1], 1)


func test_client_voice_reaches_the_server() -> void:
	assert_bool(await _connect()).override_failure_message("couldn't connect").is_true()
	var client_id := _client.multiplayer.get_unique_id()
	var copies := _spawn("Guest", client_id)
	var talker := copies[1]
	assert_bool(talker.is_local()).is_true()
	_talk(talker)
	await _assert_hears(copies[0], client_id)


func test_receivers_ignore_packets_from_other_peers() -> void:
	assert_bool(await _connect()).override_failure_message("couldn't connect").is_true()
	# The client's copy plays peer 1's voice; a packet the client gets on
	# that node from anyone else (here, itself) must not play.
	var copies := _spawn("Host", 1)
	var listener := copies[1]
	listener.transport.packet_received.emit(
		_client.multiplayer.get_unique_id(), PackedByteArray([0xF8, 0xFF, 0xFE]), 0
	)
	assert_int(listener.receiver.get_stats()["packets_received"]).is_equal(0)
	listener.transport.packet_received.emit(1, PackedByteArray([0xF8, 0xFF, 0xFE]), 0)
	assert_int(listener.receiver.get_stats()["packets_received"]).is_equal(1)
