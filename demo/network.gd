extends Control
## Voice chat between instances over ENet. Run one instance as host and
## others as clients (Debug > Customize Run Instances, or export). Each peer
## gets a VoicePeer named after its id, owned by that peer, so the same
## path exists everywhere: it sends on its owner and plays everywhere else.

const DEFAULT_PORT := 24565

# Encoder preset for this peer's own voice; the live sender keeps a copy.
var _encoder := VoiceEncoderSettings.new()
# VoicePeers live here, apart from the UI, so their RPC paths match on every
# peer no matter how the UI is laid out.
var _voices := Node.new()
var _local_voice: VoicePeer
var _transmitting := true
var _rows := {}  # peer id -> Row

var _address := LineEdit.new()
var _port := SpinBox.new()
var _host := Button.new()
var _join := Button.new()
var _leave := Button.new()
var _status := Label.new()
var _peer_list := VBoxContainer.new()
var _empty := DemoUI.muted("Nobody here yet.")


## One peer's line in the list.
class Row:
	extends HBoxContainer

	var voice: VoicePeer
	var light := DemoUI.Light.new()
	var meter := DemoUI.Meter.new()
	var info := Label.new()

	func _init(peer_id: int, local: bool) -> void:
		add_theme_constant_override("separation", 12)
		add_child(light)
		var name_label := Label.new()
		name_label.text = "Peer %d%s" % [peer_id, "  (you)" if local else ""]
		name_label.custom_minimum_size.x = 170
		add_child(name_label)
		meter.custom_minimum_size.x = 200
		meter.size_flags_horizontal = Control.SIZE_FILL
		add_child(meter)
		info.add_theme_color_override("font_color", DemoUI.MUTED)
		info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		info.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		add_child(info)

	func attach(p_voice: VoicePeer) -> void:
		voice = p_voice
		voice.speaking_started.connect(light.set_on.bind(true))
		voice.speaking_ended.connect(light.set_on.bind(false))
		voice.level_changed.connect(meter.set_level)

	func refresh() -> void:
		if voice == null:
			return
		if voice.is_local():
			var sent: Dictionary = voice.sender.get_stats()
			info.text = "%d sent, %d silent" % [sent["packets_sent"], sent["frames_silent"]]
		else:
			var s: Dictionary = voice.receiver.get_stats()
			info.text = (
				"%.0f ms buffered   lost %d   DRED %d  FEC %d  PLC %d"
				% [
					s["depth_ms"],
					s["frames_concealed"],
					s["frames_dred"],
					s["frames_fec"],
					s["frames_plc"]
				]
			)


func _ready() -> void:
	_voices.name = "Voices"
	add_child(_voices)
	_build_ui()
	multiplayer.peer_connected.connect(_add_peer)
	multiplayer.peer_disconnected.connect(_remove_peer)
	multiplayer.connected_to_server.connect(_on_connected)
	multiplayer.connection_failed.connect(_on_connection_failed)
	multiplayer.server_disconnected.connect(_leave_session.bind("The host left."))


func _exit_tree() -> void:
	multiplayer.multiplayer_peer = null


func _process(_delta: float) -> void:
	for row in _rows.values():
		row.refresh()


func _build_ui() -> void:
	var body := DemoUI.page(
		self, "Network", "Voice chat over ENet. Host in one instance, join from the others."
	)
	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 16)
	body.add_child(columns)

	var side := VBoxContainer.new()
	side.custom_minimum_size.x = 420
	side.add_theme_constant_override("separation", 16)
	columns.add_child(side)

	var session := DemoUI.card(side, "Session")
	var address_row := HBoxContainer.new()
	address_row.add_theme_constant_override("separation", 8)
	session.add_child(address_row)
	_address.text = "127.0.0.1"
	_address.placeholder_text = "Host address"
	_address.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	address_row.add_child(_address)
	_port.min_value = 1024
	_port.max_value = 65535
	_port.value = DEFAULT_PORT
	_port.custom_minimum_size.x = 110
	address_row.add_child(_port)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	session.add_child(buttons)
	for entry in [[_host, "Host", _start_host], [_join, "Join", _start_join]]:
		var button: Button = entry[0]
		button.text = entry[1]
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(entry[2])
		buttons.add_child(button)
	_leave.text = "Leave"
	_leave.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_leave.pressed.connect(_leave_session.bind("Left the session."))
	buttons.add_child(_leave)
	_status.text = "Not connected."
	_status.add_theme_color_override("font_color", DemoUI.MUTED)
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	session.add_child(_status)

	var voice := DemoUI.card(side, "Your voice")
	DemoUI.toggle(voice, "Microphone on", true, _set_transmitting)
	DemoUI.slider(
		voice,
		"Bitrate",
		6000,
		128000,
		1000,
		_encoder.bitrate,
		"kb/s",
		func(v): _set_encoder(&"bitrate", int(v))
	)
	DemoUI.slider(
		voice,
		"Expected loss",
		0,
		50,
		1,
		_encoder.expected_packet_loss_percent,
		"%",
		func(v): _set_encoder(&"expected_packet_loss_percent", int(v))
	)
	var dred := DemoUI.slider(
		voice,
		"DRED history",
		0,
		1000,
		10,
		_encoder.dred_duration_ms,
		"ms",
		func(v): _set_encoder(&"dred_duration_ms", int(v))
	)
	if not DemoUI.has_dnn():
		dred.editable = false
		voice.add_child(DemoUI.muted("DRED needs a DNN build (scons dnn=yes)."))

	var peers := DemoUI.card(columns, "Peers", true)
	peers.get_parent().size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_peer_list.add_theme_constant_override("separation", 10)
	peers.add_child(_peer_list)
	peers.add_child(_empty)
	_set_connected(false)


func _start_host() -> void:
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_server(int(_port.value))
	if error != OK:
		_status.text = "Couldn't host on port %d: %s." % [_port.value, error_string(error)]
		return
	multiplayer.multiplayer_peer = peer
	_set_connected(true)
	_status.text = "Hosting on port %d. Others can join this machine's address." % _port.value
	_add_peer(1)


func _start_join() -> void:
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_client(_address.text, int(_port.value))
	if error != OK:
		_status.text = "Couldn't connect: %s." % error_string(error)
		return
	multiplayer.multiplayer_peer = peer
	_set_connected(true)
	_status.text = "Connecting to %s:%d..." % [_address.text, _port.value]


func _on_connected() -> void:
	_status.text = "Connected as peer %d." % multiplayer.get_unique_id()
	_add_peer(multiplayer.get_unique_id())


func _on_connection_failed() -> void:
	_leave_session("Connection failed.")


func _leave_session(message: String) -> void:
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()  # tells the others right away
	multiplayer.multiplayer_peer = null
	for id in _rows.keys():
		_remove_peer(id)
	_set_connected(false)
	_status.text = message


func _set_connected(connected: bool) -> void:
	_host.visible = not connected
	_join.visible = not connected
	_leave.visible = connected
	_address.editable = not connected
	_port.editable = not connected


func _add_peer(id: int) -> void:
	if _rows.has(id):
		return
	var local := id == multiplayer.get_unique_id()
	var row := Row.new(id, local)
	_peer_list.add_child(row)
	_rows[id] = row
	_empty.hide()

	var voice := VoicePeer.new()
	voice.name = str(id)
	voice.encoder_settings = _encoder
	var player := AudioStreamPlayer.new()
	voice.add_child(player)
	voice.players = [player]
	# Before entering the tree: VoicePeer picks its role in _ready.
	voice.set_multiplayer_authority(id)
	row.attach(voice)
	_voices.add_child(voice)
	if local:
		_local_voice = voice
		voice.sender.transmitting = _transmitting


func _remove_peer(id: int) -> void:
	if not _rows.has(id):
		return
	var row: Row = _rows[id]
	_rows.erase(id)
	if row.voice == _local_voice:
		_local_voice = null
	row.voice.queue_free()
	row.queue_free()
	_empty.visible = _rows.is_empty()


func _set_transmitting(on: bool) -> void:
	_transmitting = on
	if _local_voice != null:
		_local_voice.sender.transmitting = on


# Updates the preset (for a voice created later) and the running sender's copy.
func _set_encoder(property: StringName, value: int) -> void:
	_encoder.set(property, value)
	if _local_voice != null:
		_local_voice.sender.settings.set(property, value)
