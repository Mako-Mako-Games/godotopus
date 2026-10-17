extends GdUnitTestSuite
## Tests for the VoiceCapturePipeline binding. Framing, resampling and DTX
## gating are covered by the native suite (tests/native); these check the
## Godot-facing API.


func _process_seconds(
	pipeline: VoiceCapturePipeline, source: TestSignals.Source
) -> Array[PackedByteArray]:
	var packets: Array[PackedByteArray] = []
	while not source.is_finished():
		packets.append_array(pipeline.process(source.read()))
	return packets


func test_class_is_registered() -> void:
	assert_bool(ClassDB.class_exists(&"VoiceCapturePipeline")).is_true()


func test_tone_becomes_one_packet_per_frame() -> void:
	var pipeline := VoiceCapturePipeline.new()
	pipeline.dtx = false
	var packets := _process_seconds(
		pipeline, TestSignals.Source.new(TestSignals.tone(440.0), 48000, 1.0)
	)
	assert_int(packets.size()).is_equal(50)

	var decoder := GodotOpusDecoder.new()
	decoder.initialize(48000, 1)
	for packet in packets:
		assert_int(decoder.decode(packet).size()).is_equal(960)
	var stats := pipeline.get_stats()
	assert_int(stats["frames_encoded"]).is_equal(50)
	assert_int(stats["packets_sent"]).is_equal(50)
	assert_int(stats["errors"]).is_equal(0)
	assert_float(pipeline.get_level()).is_greater(0.3)


func test_resamples_from_the_input_rate() -> void:
	var pipeline := VoiceCapturePipeline.new()
	pipeline.dtx = false
	pipeline.input_mix_rate = 44100
	pipeline.sample_rate = 16000
	pipeline.frame_duration_ms = 40
	var packets := _process_seconds(
		pipeline, TestSignals.Source.new(TestSignals.tone(440.0), 44100, 1.0)
	)
	assert_int(packets.size()).is_between(24, 25)


func test_silence_sends_nothing() -> void:
	var pipeline := VoiceCapturePipeline.new()
	var packets := _process_seconds(
		pipeline, TestSignals.Source.new(TestSignals.tone(0.0, 0.0), 48000, 3.0)
	)
	# Opus sends a few frames before it classifies the input as silence.
	assert_int(packets.size()).is_less(15)
	assert_int(pipeline.get_stats()["frames_silent"]).is_greater(130)
	assert_bool(pipeline.is_speaking()).is_false()


func test_codec_changes_keep_the_encoder_and_format_changes_restart_it() -> void:
	var pipeline := VoiceCapturePipeline.new()
	pipeline.dtx = false
	var source := TestSignals.Source.new(TestSignals.tone(440.0), 48000, 10.0)
	pipeline.process(source.read())

	pipeline.bitrate = 64000
	pipeline.complexity = 3
	pipeline.inband_fec = false
	pipeline.expected_packet_loss = 25
	pipeline.process(source.read())
	assert_int(pipeline.get_stats()["encoder_restarts"]).is_equal(0)

	pipeline.sample_rate = 24000
	pipeline.frame_duration_ms = 10
	pipeline.process(source.read())
	assert_int(pipeline.get_stats()["encoder_restarts"]).is_equal(1)


func test_settings_are_validated() -> void:
	var pipeline := VoiceCapturePipeline.new()
	pipeline.sample_rate = 44100
	assert_int(pipeline.sample_rate).is_equal(48000)
	pipeline.frame_duration_ms = 30
	assert_int(pipeline.frame_duration_ms).is_equal(20)
	pipeline.channels = 7
	assert_int(pipeline.channels).is_equal(1)
	pipeline.complexity = -4
	assert_int(pipeline.complexity).is_equal(0)


func test_stats_report_every_counter() -> void:
	var stats := VoiceCapturePipeline.new().get_stats()
	for key in [
		"frames_encoded",
		"packets_sent",
		"frames_silent",
		"dropped_ms",
		"encoder_restarts",
		"errors"
	]:
		assert_bool(stats.has(key)).override_failure_message("missing %s" % key).is_true()
