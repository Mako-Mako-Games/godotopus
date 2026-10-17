extends Control
## Picks a demo.


func _ready() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	box.add_theme_constant_override("separation", 12)
	add_child(box)

	var title := Label.new()
	title.text = "Godotopus demos"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(title)

	for entry in [
		["Loopback: hear yourself through a simulated network", "res://loopback.tscn"],
		["Network: voice chat between instances over ENet", "res://network.tscn"],
	]:
		var button := Button.new()
		button.text = entry[0]
		button.pressed.connect(get_tree().change_scene_to_file.bind(entry[1]))
		box.add_child(button)
