extends SceneTree
## Checks that the extension loads and its classes work at a basic level.
## Run with: godot --headless --path tests/godot --script res://smoke_test.gd
## (GdUnit4 suites replace this from Stage 1 on.)

const CLASSES: Array[StringName] = [&"GodotOpusEncoder", &"GodotOpusDecoder", &"GodotOpusResampler"]


func _initialize() -> void:
	var failures := PackedStringArray()

	for cls in CLASSES:
		if not ClassDB.class_exists(cls):
			failures.append("class %s is not registered" % cls)

	if failures.is_empty():
		failures.append_array(_check_round_trip())

	for failure in failures:
		printerr("FAIL: ", failure)
	if failures.is_empty():
		print("Smoke test passed.")
	quit(1 if not failures.is_empty() else 0)


func _check_round_trip() -> PackedStringArray:
	var failures := PackedStringArray()
	var encoder: Object = ClassDB.instantiate(&"GodotOpusEncoder")
	var decoder: Object = ClassDB.instantiate(&"GodotOpusDecoder")
	encoder.initialize(48000, 1, 20)
	decoder.initialize(48000, 1, 20)

	var pcm := PackedFloat32Array()
	pcm.resize(960)
	for i in pcm.size():
		pcm[i] = 0.5 * sin(TAU * 440.0 * i / 48000.0)

	var packet: PackedByteArray = encoder.encode(pcm)
	if packet.size() <= 2:
		failures.append("encoding a tone produced %d bytes" % packet.size())
		return failures

	var decoded: PackedFloat32Array = decoder.decode(packet)
	if decoded.size() != 960:
		failures.append("decoded %d samples, expected 960" % decoded.size())
	return failures
