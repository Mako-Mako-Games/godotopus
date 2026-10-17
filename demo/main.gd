extends Control
## Picks a demo.

const LIGHT_BUILD_NOTE := (
	"This is the light build, without the neural-network features. Build with "
	+ "`scons dnn=yes install` to try Deep REDundancy, deep PLC and OSCE."
)
const DEMOS := [
	[
		"Loopback",
		(
			"Hear yourself through a simulated network. Add loss, delay and jitter, "
			+ "change encoder settings live, and watch how the receiver recovers "
			+ "(DRED, FEC or PLC)."
		),
		"res://loopback.tscn",
	],
	[
		"Network",
		(
			"Voice chat between instances over ENet. Host in one, join from the others "
			+ "(Debug > Customize Run Instances runs several at once)."
		),
		"res://network.tscn",
	],
]


func _ready() -> void:
	var body := DemoUI.page(self, "Godotopus", "Opus voice chat for Godot. Use headphones.", false)
	var cards := HBoxContainer.new()
	cards.add_theme_constant_override("separation", 16)
	body.add_child(cards)
	for demo in DEMOS:
		var content := DemoUI.card(cards, demo[0])
		content.get_parent().size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var description := Label.new()
		description.text = demo[1]
		description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		description.custom_minimum_size.x = 200
		description.size_flags_vertical = Control.SIZE_EXPAND_FILL
		content.add_child(description)
		var open := Button.new()
		open.text = "Open %s" % demo[0]
		open.size_flags_horizontal = Control.SIZE_SHRINK_END
		open.pressed.connect(get_tree().change_scene_to_file.bind(demo[2]))
		content.add_child(open)

	if not DemoUI.has_dnn():
		body.add_child(DemoUI.muted(LIGHT_BUILD_NOTE))
