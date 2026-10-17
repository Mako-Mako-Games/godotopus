extends Control
## Your microphone -> VoiceSender -> VoiceLoopbackTransport (with simulated
## loss, delay and jitter) -> VoiceReceiver -> a dry player and, optionally,
## a second player on a radio-effect bus.

const DRED_HINT := (
	"Opus only spends bits on DRED with enough bitrate and expected loss: in testing it "
	+ "started around 34 kb/s at 20% expected loss, and not at all below 10%. Try 40 kb/s, "
	+ "20% expected loss and 500 ms of history, then add packet loss."
)
const SENDER_STATS := [
	["packets_sent", "Packets sent"],
	["frames_silent", "Silent frames (not sent)"],
	["dropped_ms", "Backlog dropped (ms)"],
	["encoder_restarts", "Encoder restarts"],
	["errors", "Errors"],
]
const RECEIVER_STATS := [
	["packets_received", "Packets received"],
	["packets_played", "Packets played"],
	["frames_concealed", "Frames concealed"],
	["packets_late", "Late"],
	["packets_dropped", "Dropped (over latency)"],
	["packets_skipped", "Skipped (long gaps)"],
	["underruns", "Underruns"],
	["depth_ms", "Buffered (ms)"],
]

var _sender := VoiceSender.new()
var _transport := VoiceLoopbackTransport.new()
var _receiver := VoiceReceiver.new()
var _dry := AudioStreamPlayer.new()
var _radio := AudioStreamPlayer.new()

var _sender_light := DemoUI.Light.new()
var _sender_meter := DemoUI.Meter.new()
var _sender_stats := DemoUI.StatsGrid.new(SENDER_STATS)
var _receiver_light := DemoUI.Light.new()
var _receiver_meter := DemoUI.Meter.new()
var _receiver_stats := DemoUI.StatsGrid.new(RECEIVER_STATS)
var _recovery := RichTextLabel.new()
var _lost := Label.new()


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

	_sender.speaking_started.connect(_sender_light.set_on.bind(true))
	_sender.speaking_ended.connect(_sender_light.set_on.bind(false))
	_sender.level_changed.connect(_sender_meter.set_level)
	_receiver.speaking_started.connect(_receiver_light.set_on.bind(true))
	_receiver.speaking_ended.connect(_receiver_light.set_on.bind(false))
	_receiver.level_changed.connect(_receiver_meter.set_level)
	_build_ui()


func _process(_delta: float) -> void:
	_sender_stats.update(_sender.get_stats())
	var stats := _receiver.get_stats()
	_receiver_stats.update(stats)
	_recovery.text = (
		"[color=#%s]DRED %d[/color]   [color=#%s]FEC %d[/color]   [color=#%s]PLC %d[/color]"
		% [
			DemoUI.GOOD.to_html(false),
			stats["frames_dred"],
			DemoUI.ACCENT.to_html(false),
			stats["frames_fec"],
			DemoUI.WARN.to_html(false),
			stats["frames_plc"],
		]
	)
	_lost.text = "%d of %d lost on the wire" % [_transport.packets_lost, _transport.packets_sent]


func _build_ui() -> void:
	var body := DemoUI.page(
		self,
		"Loopback",
		"Microphone → encoder → simulated network → jitter buffer and decoder → speakers."
	)
	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 16)
	body.add_child(columns)

	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size.x = 470
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	columns.add_child(scroll)
	var controls := VBoxContainer.new()
	controls.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	controls.add_theme_constant_override("separation", 16)
	scroll.add_child(controls)
	_build_network(DemoUI.card(controls, "Network"))
	_build_encoder(DemoUI.card(controls, "Sender"))
	_build_receiver(DemoUI.card(controls, "Receiver"))

	var monitor := VBoxContainer.new()
	monitor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	monitor.add_theme_constant_override("separation", 16)
	columns.add_child(monitor)
	_build_monitor(monitor, "Sender", _sender_light, _sender_meter, _sender_stats)
	var receiver := _build_monitor(
		monitor, "Receiver", _receiver_light, _receiver_meter, _receiver_stats
	)
	receiver.add_child(HSeparator.new())
	receiver.add_child(DemoUI.muted("Lost frames recovered with"))
	_recovery.bbcode_enabled = true
	_recovery.fit_content = true
	_recovery.scroll_active = false
	receiver.add_child(_recovery)


func _build_network(card: VBoxContainer) -> void:
	DemoUI.slider(card, "Packet loss", 0, 50, 1, 0, "%", func(v): _transport.loss_percent = v)
	DemoUI.slider(card, "Delay", 0, 500, 10, 0, "ms", func(v): _transport.delay_ms = int(v))
	DemoUI.slider(card, "Jitter", 0, 200, 5, 0, "ms", func(v): _transport.jitter_ms = int(v))
	_lost.add_theme_color_override("font_color", DemoUI.MUTED)
	card.add_child(_lost)


func _build_encoder(card: VBoxContainer) -> void:
	var settings := _sender.settings
	DemoUI.slider(
		card,
		"Bitrate",
		6000,
		128000,
		1000,
		settings.bitrate,
		"kb/s",
		func(v): _sender.settings.bitrate = int(v)
	)
	DemoUI.options(
		card,
		"Frame duration",
		VoiceEncoderSettings.FRAME_DURATIONS,
		settings.frame_duration_ms,
		"ms",
		func(v): _sender.settings.frame_duration_ms = v
	)
	DemoUI.options(
		card,
		"Encode rate",
		VoiceEncoderSettings.SAMPLE_RATES,
		settings.sample_rate,
		"Hz",
		func(v): _sender.settings.sample_rate = v
	)
	DemoUI.slider(
		card,
		"Expected loss",
		0,
		50,
		1,
		settings.expected_packet_loss_percent,
		"%",
		func(v): _sender.settings.expected_packet_loss_percent = int(v)
	)
	var dred := DemoUI.slider(
		card,
		"DRED history",
		0,
		1000,
		10,
		settings.dred_duration_ms,
		"ms",
		func(v): _sender.settings.dred_duration_ms = int(v)
	)
	if DemoUI.has_dnn():
		card.add_child(DemoUI.muted(DRED_HINT))
	else:
		dred.editable = false
		card.add_child(DemoUI.muted("DRED needs a DNN build (scons dnn=yes)."))
	DemoUI.toggle(card, "Skip silence (DTX)", settings.dtx, func(on): _sender.settings.dtx = on)
	DemoUI.toggle(
		card, "In-band FEC", settings.inband_fec, func(on): _sender.settings.inband_fec = on
	)
	DemoUI.toggle(card, "Transmit", true, func(on): _sender.transmitting = on)


func _build_receiver(card: VBoxContainer) -> void:
	var settings := _receiver.settings
	DemoUI.slider(
		card,
		"Target latency",
		0,
		300,
		10,
		settings.target_latency_ms,
		"ms",
		func(v): _receiver.settings.target_latency_ms = int(v)
	)
	DemoUI.slider(
		card,
		"Decoder complexity",
		0,
		10,
		1,
		settings.decoder_complexity,
		"",
		func(v): _receiver.settings.decoder_complexity = int(v)
	)
	DemoUI.toggle(card, "Also play on a radio-effect bus", false, _set_radio)


func _build_monitor(
	parent: Node, title: String, light: Control, meter: Control, stats: Control
) -> VBoxContainer:
	var card := DemoUI.card(parent, title)
	var title_row: HBoxContainer = card.get_child(0)
	title_row.add_child(light)
	title_row.move_child(light, 0)
	card.add_child(meter)
	card.add_child(stats)
	return card


func _set_radio(on: bool) -> void:
	if on:
		_receiver.add_player(_radio)
	else:
		_receiver.remove_player(_radio)
