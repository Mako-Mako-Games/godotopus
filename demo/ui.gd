class_name DemoUI
extends RefCounted
## Theme and widgets shared by the demo pages, built in code so the scenes
## stay trivial.

const BACKGROUND := Color("13151a")
const SURFACE := Color("1c1f26")
const SURFACE_RAISED := Color("262a33")
const BORDER := Color("2e333e")
const TEXT := Color("e7e9ee")
const MUTED := Color("8b91a1")
const ACCENT := Color("4fb3ff")
const GOOD := Color("4cd38a")
const WARN := Color("f2b84b")
const IDLE := Color("3a3f4b")

const MAIN_SCENE := "res://main.tscn"


## A speaking indicator: a dot that lights up.
class Light:
	extends Panel

	var _style := StyleBoxFlat.new()

	func _init() -> void:
		custom_minimum_size = Vector2(12, 12)
		size_flags_vertical = Control.SIZE_SHRINK_CENTER
		_style.set_corner_radius_all(6)
		_style.bg_color = DemoUI.IDLE
		add_theme_stylebox_override("panel", _style)

	func set_on(on: bool) -> void:
		_style.bg_color = DemoUI.GOOD if on else DemoUI.IDLE


## A level meter in dBFS, -60 to 0, that falls back smoothly.
class Meter:
	extends ProgressBar

	const FLOOR_DB := -60.0

	func _init() -> void:
		min_value = FLOOR_DB
		max_value = 0.0
		value = FLOOR_DB
		show_percentage = false
		custom_minimum_size = Vector2(120, 8)
		size_flags_horizontal = Control.SIZE_EXPAND_FILL
		size_flags_vertical = Control.SIZE_SHRINK_CENTER

	func set_level(rms: float) -> void:
		var db := FLOOR_DB if rms <= 0.0 else maxf(FLOOR_DB, 20.0 * log(rms) / log(10.0))
		value = maxf(db, value - 1.5)


## Rows of label/value pairs, refreshed from a stats Dictionary.
class StatsGrid:
	extends GridContainer

	var _values := {}

	func _init(rows: Array) -> void:
		columns = 2
		add_theme_constant_override("h_separation", 24)
		add_theme_constant_override("v_separation", 2)
		for row in rows:
			var key: String = row[0]
			var name_label := Label.new()
			name_label.text = row[1]
			name_label.add_theme_color_override("font_color", DemoUI.MUTED)
			add_child(name_label)
			var value_label := Label.new()
			value_label.text = "-"
			value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
			value_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			add_child(value_label)
			_values[key] = value_label

	func update(stats: Dictionary) -> void:
		for key in _values:
			if stats.has(key):
				var value: Variant = stats[key]
				_values[key].text = "%.0f" % value if value is float else str(value)


static func make_theme() -> Theme:
	var theme := Theme.new()
	theme.default_font_size = 15

	var panel := _box(SURFACE, 10, 16)
	panel.border_color = BORDER
	panel.set_border_width_all(1)
	theme.set_stylebox("panel", "PanelContainer", panel)
	theme.set_color("font_color", "Label", TEXT)

	for type in ["Button", "OptionButton"]:
		theme.set_stylebox("normal", type, _box(SURFACE_RAISED, 6, 8))
		theme.set_stylebox("hover", type, _box(SURFACE_RAISED.lightened(0.08), 6, 8))
		theme.set_stylebox("pressed", type, _box(ACCENT.darkened(0.45), 6, 8))
		theme.set_stylebox("disabled", type, _box(SURFACE, 6, 8))
		theme.set_stylebox("focus", type, StyleBoxEmpty.new())
		theme.set_color("font_color", type, TEXT)
		theme.set_color("font_disabled_color", type, MUTED)

	# Toggles inherit Button's boxes otherwise; they should look like rows.
	for state in ["normal", "hover", "pressed", "hover_pressed", "focus"]:
		theme.set_stylebox(state, "CheckButton", StyleBoxEmpty.new())
	for state in [
		"font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color"
	]:
		theme.set_color(state, "CheckButton", TEXT)

	theme.set_stylebox("normal", "LineEdit", _box(BACKGROUND, 6, 8))
	theme.set_stylebox("focus", "LineEdit", StyleBoxEmpty.new())
	theme.set_stylebox("background", "ProgressBar", _box(BACKGROUND, 4, 0))
	theme.set_stylebox("fill", "ProgressBar", _box(ACCENT, 4, 0))
	return theme


## Builds the page frame on [param root]: background, header with title and
## build badge, and a body container for the page's content.
static func page(root: Control, title: String, subtitle: String, back := true) -> VBoxContainer:
	root.theme = make_theme()
	var background := ColorRect.new()
	background.name = "Background"
	background.color = BACKGROUND
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_child(background)

	var margin := MarginContainer.new()
	margin.name = "Margin"
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 24)
	root.add_child(margin)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	margin.add_child(column)

	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 16)
	column.add_child(header)
	if back:
		var back_button := Button.new()
		back_button.text = "Back"
		back_button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		back_button.pressed.connect(func(): root.get_tree().change_scene_to_file(MAIN_SCENE))
		header.add_child(back_button)
	var titles := VBoxContainer.new()
	titles.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	titles.add_theme_constant_override("separation", 2)
	header.add_child(titles)
	var title_label := Label.new()
	title_label.text = title
	title_label.add_theme_font_size_override("font_size", 26)
	titles.add_child(title_label)
	titles.add_child(muted(subtitle, 14))
	header.add_child(build_badge())

	var body := VBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 16)
	column.add_child(body)
	return body


## True if this build of the addon can encode and decode Deep REDundancy,
## i.e. it was built with dnn=yes.
static func has_dnn() -> bool:
	return VoiceCapturePipeline.new().has_dred_support()


## Shows which flavor of the addon is running.
static func build_badge() -> Control:
	var dnn := has_dnn()
	var color := GOOD if dnn else WARN
	var style := _box(Color(color, 0.12), 14, 6)
	style.content_margin_left = 14
	style.content_margin_right = 14
	style.border_color = Color(color, 0.5)
	style.set_border_width_all(1)
	var badge := PanelContainer.new()
	badge.add_theme_stylebox_override("panel", style)
	badge.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var label := Label.new()
	label.text = "DNN build: DRED, deep PLC, OSCE" if dnn else "Light build: no DRED"
	label.add_theme_color_override("font_color", color)
	label.add_theme_font_size_override("font_size", 13)
	badge.add_child(label)
	return badge


## A titled card; returns the container to put its content in. The title
## row is its first child, for adding indicators next to the title.
static func card(parent: Node, title: String, expand := false) -> VBoxContainer:
	var panel := PanelContainer.new()
	if expand:
		panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	parent.add_child(panel)
	var content := VBoxContainer.new()
	content.add_theme_constant_override("separation", 10)
	panel.add_child(content)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	content.add_child(row)
	var label := Label.new()
	label.text = title.to_upper()
	label.add_theme_color_override("font_color", MUTED)
	label.add_theme_font_size_override("font_size", 12)
	row.add_child(label)
	return content


static func muted(text: String, size := 13) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_color_override("font_color", MUTED)
	label.add_theme_font_size_override("font_size", size)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label


## A labelled slider showing its value; calls [param apply] with each value.
static func slider(
	parent: Node,
	label: String,
	low: float,
	high: float,
	step: float,
	value: float,
	suffix: String,
	apply: Callable
) -> HSlider:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	parent.add_child(row)
	var name_label := Label.new()
	name_label.text = label
	name_label.custom_minimum_size.x = 150
	row.add_child(name_label)
	var control := HSlider.new()
	control.min_value = low
	control.max_value = high
	control.step = step
	control.value = value
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	control.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	row.add_child(control)
	var value_label := Label.new()
	value_label.custom_minimum_size.x = 84
	value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	value_label.text = _format(value, suffix)
	row.add_child(value_label)
	control.value_changed.connect(
		func(v: float):
			value_label.text = _format(v, suffix)
			apply.call(v)
	)
	return control


## A labelled dropdown of integer values; calls [param apply] with the choice.
static func options(
	parent: Node, label: String, values: Array[int], value: int, suffix: String, apply: Callable
) -> OptionButton:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	parent.add_child(row)
	var name_label := Label.new()
	name_label.text = label
	name_label.custom_minimum_size.x = 150
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(name_label)
	var control := OptionButton.new()
	control.custom_minimum_size.x = 120
	for v in values:
		control.add_item(_format(v, suffix))
	control.select(values.find(value))
	control.item_selected.connect(func(index: int): apply.call(values[index]))
	row.add_child(control)
	return control


static func toggle(parent: Node, label: String, on: bool, apply: Callable) -> CheckButton:
	var control := CheckButton.new()
	control.text = label
	control.button_pressed = on
	control.toggled.connect(apply)
	parent.add_child(control)
	return control


static func _format(value: float, suffix: String) -> String:
	if suffix == "kb/s":
		return "%.0f kb/s" % (value / 1000.0)
	return ("%d %s" % [int(value), suffix]).strip_edges()


static func _box(color: Color, radius: int, margin: float) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(radius)
	style.set_content_margin_all(margin)
	return style
