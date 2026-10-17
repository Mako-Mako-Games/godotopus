class_name DemoAudio
extends RefCounted
## Audio setup shared by the demos.

const RADIO_BUS := &"DemoRadio"


## A bus that makes voices sound like a handheld radio.
static func radio_bus() -> StringName:
	if AudioServer.get_bus_index(RADIO_BUS) < 0:
		AudioServer.add_bus()
		var bus := AudioServer.bus_count - 1
		AudioServer.set_bus_name(bus, RADIO_BUS)
		var band := AudioEffectBandPassFilter.new()
		band.cutoff_hz = 1800.0
		band.resonance = 0.8
		AudioServer.add_bus_effect(bus, band)
		var crunch := AudioEffectDistortion.new()
		crunch.mode = AudioEffectDistortion.MODE_OVERDRIVE
		crunch.drive = 0.4
		AudioServer.add_bus_effect(bus, crunch)
	return RADIO_BUS


## One line per stat, for a monospace label.
static func format_stats(stats: Dictionary) -> String:
	var lines := PackedStringArray()
	for key in stats:
		var value = stats[key]
		lines.append("%-18s %s" % [key, ("%.1f" % value) if value is float else str(value)])
	return "\n".join(lines)
