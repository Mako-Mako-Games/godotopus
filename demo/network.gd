extends Control
## Voice chat between instances over ENet. Run one instance as host and
## others as clients (Debug > Customize Run Instances, or export). Each peer
## gets a VoicePeer named after its id, owned by that peer, so the same
## path exists everywhere: it sends on its owner and plays everywhere else.

const PORT := 24565

var _address := LineEdit.new()
var _status := Label.new()
var _peers := VBoxContainer.new()
var _connect_row := HBoxContainer.new()


func _ready() -> void:
	_build_ui()
	multiplayer.peer_connected.connect(_add_peer)
	multiplayer.peer_disconnected.connect(_remove_peer)
	multiplayer.connected_to_server.connect(_on_connected)
	multiplayer.connection_failed.connect(_set_status.bind("Connection failed."))
	multiplayer.server_disconnected.connect(_on_server_disconnected)


func _exit_tree() -> void:
	multiplayer.multiplayer_peer = null


func _build_ui() -> void:
	# VoicePeers must sit at the same path on every peer, so nothing above
	# them can have an auto-generated name.
	var margin := MarginContainer.new()
	margin.name = "Margin"
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 16)
	add_child(margin)
	var root := VBoxContainer.new()
	root.name = "Root"
	margin.add_child(root)

	var back := Button.new()
	back.text = "< Back"
	back.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	back.pressed.connect(get_tree().change_scene_to_file.bind("res://main.tscn"))
	root.add_child(back)

	root.add_child(_connect_row)
	var host := Button.new()
	host.text = "Host"
	host.pressed.connect(_host)
	_connect_row.add_child(host)
	_address.text = "127.0.0.1"
	_address.custom_minimum_size.x = 200
	_connect_row.add_child(_address)
	var join := Button.new()
	join.text = "Join"
	join.pressed.connect(_join)
	_connect_row.add_child(join)

	_status.text = "Host, or join a host. Use headphones."
	root.add_child(_status)
	_peers.name = "Peers"
	root.add_child(_peers)


func _host() -> void:
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_server(PORT)
	if error != OK:
		_set_status("Couldn't host on port %d: %s" % [PORT, error_string(error)])
		return
	multiplayer.multiplayer_peer = peer
	_connect_row.hide()
	_set_status("Hosting on port %d." % PORT)
	_add_peer(1)


func _join() -> void:
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_client(_address.text, PORT)
	if error != OK:
		_set_status("Couldn't connect: %s" % error_string(error))
		return
	multiplayer.multiplayer_peer = peer
	_connect_row.hide()
	_set_status("Connecting to %s..." % _address.text)


func _on_connected() -> void:
	_set_status("Connected as peer %d." % multiplayer.get_unique_id())
	_add_peer(multiplayer.get_unique_id())


func _on_server_disconnected() -> void:
	_set_status("Host left.")
	for child in _peers.get_children():
		child.queue_free()
	multiplayer.multiplayer_peer = null
	_connect_row.show()


func _add_peer(id: int) -> void:
	var row_name := str(id)
	if _peers.has_node(row_name):
		return
	var row := HBoxContainer.new()
	row.name = row_name
	_peers.add_child(row)

	var light := ColorRect.new()
	light.custom_minimum_size = Vector2(14, 14)
	light.color = Color.DIM_GRAY
	row.add_child(light)
	var label := Label.new()
	label.text = "Peer %d%s" % [id, " (you)" if id == multiplayer.get_unique_id() else ""]
	label.custom_minimum_size.x = 160
	row.add_child(label)
	var level := ProgressBar.new()
	level.max_value = 0.3
	level.show_percentage = false
	level.custom_minimum_size = Vector2(200, 14)
	row.add_child(level)

	var player := AudioStreamPlayer.new()
	row.add_child(player)
	var voice := VoicePeer.new()
	voice.name = "Voice"
	voice.players = [player]
	# Before entering the tree: VoicePeer picks its role in _ready.
	voice.set_multiplayer_authority(id)
	voice.speaking_started.connect(light.set_color.bind(Color.GREEN))
	voice.speaking_ended.connect(light.set_color.bind(Color.DIM_GRAY))
	voice.level_changed.connect(level.set_value)
	row.add_child(voice)


func _remove_peer(id: int) -> void:
	var row := _peers.get_node_or_null(str(id))
	if row != null:
		row.queue_free()


func _set_status(text: String) -> void:
	_status.text = text
