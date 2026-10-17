extends Control
## Your microphone -> VoiceSender -> VoiceLoopbackTransport (with simulated
## loss, delay and jitter) -> VoiceReceiver -> a dry player and, optionally,
## a second player on a radio-effect bus. Use headphones.

var _sender := VoiceSender.new()
var _transport := VoiceLoopbackTransport.new()
var _receiver := VoiceReceiver.new()
var _dry := AudioStreamPlayer.new()
var _radio := AudioStreamPlayer.new()
var _sender_stats := Label.new()
var _receiver_stats := Label.new()
var _sender_light := ColorRect.new()
var _receiver_light := ColorRect.new()


func _ready() -> void:
	add_child(_transport)
	_sender.transport = _transport
	add_child(_sender)
	_receiver.transport = _transport
	add_child(_receiver)
	add_child(_dry)
	_radio.bus = DemoAudio.radio_bus()
	add_child(_radio)
	_receiver.add_player(_dry)

	_sender.speaking_started.connect(_sender_light.set_color.bind(Color.GREEN))
	_sender.speaking_ended.connect(_sender_light.set_color.bind(Color.DIM_GRAY))
	_receiver.speaking_started.connect(_receiver_light.set_color.bind(Color.GREEN))
	_receiver.speaking_ended.connect(_receiver_light.set_color.bind(Color.DIM_GRAY))
	_build_ui()


func _build_ui() -> void:
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 16)
	add_child(margin)
	var root := VBoxContainer.new()
	margin.add_child(root)

	var back := Button.new()
	back.text = "< Back"
	back.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	back.pressed.connect(get_tree().change_scene_to_file.bind("res://main.tscn"))
	root.add_child(back)

	var grid := GridContainer.new()
	grid.columns = 2
	root.add_child(grid)
	_slider(grid, "Packet loss (%)", 0, 50, 0, func(v): _transport.loss_percent = v)
	_slider(grid, "Delay (ms)", 0, 500, 0, func(v): _transport.delay_ms = int(v))
	_slider(grid, "Jitter (ms)", 0, 200, 0, func(v): _transport.jitter_ms = int(v))
	_slider(grid, "Bitrate (b/s)", 6000, 64000, 24000, func(v): _sender.settings.bitrate = int(v))
	_options(
		grid,
		"Frame duration (ms)",
		VoiceEncoderSettings.FRAME_DURATIONS,
		20,
		func(v): _sender.settings.frame_duration_ms = v
	)
	_options(
		grid,
		"Encode rate (Hz)",
		VoiceEncoderSettings.SAMPLE_RATES,
		48000,
		func(v): _sender.settings.sample_rate = v
	)
	_slider(
		grid,
		"Receiver latency (ms)",
		0,
		300,
		60,
		func(v): _receiver.settings.target_latency_ms = int(v)
	)

	var radio := CheckBox.new()
	radio.text = "Also play through a radio-effect bus (one voice, two players)"
	radio.toggled.connect(_set_radio)
	root.add_child(radio)
	var transmit := CheckBox.new()
	transmit.text = "Transmit (untick for push-to-talk style muting)"
	transmit.button_pressed = true
	transmit.toggled.connect(func(on): _sender.transmitting = on)
	root.add_child(transmit)

	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(columns)
	for pair in [
		["Sender", _sender_light, _sender_stats], ["Receiver", _receiver_light, _receiver_stats]
	]:
		var column := VBoxContainer.new()
		column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		columns.add_child(column)
		var header := HBoxContainer.new()
		column.add_child(header)
		var light: ColorRect = pair[1]
		light.custom_minimum_size = Vector2(14, 14)
		light.color = Color.DIM_GRAY
		header.add_child(light)
		var title := Label.new()
		title.text = pair[0]
		header.add_child(title)
		var stats: Label = pair[2]
		var font := SystemFont.new()
		font.font_names = PackedStringArray(["monospace"])
		stats.add_theme_font_override("font", font)
		column.add_child(stats)


func _set_radio(on: bool) -> void:
	if on:
		_receiver.add_player(_radio)
	else:
		_receiver.remove_player(_radio)


func _process(_delta: float) -> void:
	_sender_stats.text = DemoAudio.format_stats(_sender.get_stats())
	_receiver_stats.text = DemoAudio.format_stats(_receiver.get_stats())


func _slider(
	parent: Node, label: String, low: float, high: float, value: float, apply: Callable
) -> void:
	var text := Label.new()
	parent.add_child(text)
	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.value = value
	slider.custom_minimum_size.x = 360
	slider.value_changed.connect(
		func(v):
			apply.call(v)
			text.text = "%s: %d" % [label, v]
	)
	text.text = "%s: %d" % [label, value]
	parent.add_child(slider)


func _options(parent: Node, label: String, values: Array[int], value: int, apply: Callable) -> void:
	var text := Label.new()
	text.text = label
	parent.add_child(text)
	var options := OptionButton.new()
	for v in values:
		options.add_item(str(v))
	options.select(values.find(value))
	options.item_selected.connect(func(index): apply.call(values[index]))
	parent.add_child(options)
