extends GdUnitTestSuite
## Tests for the GodotOpusEncoder / GodotOpusDecoder bindings. The codec
## itself is covered by the native suite (tests/native); these check what
## GDScript sees.

const DECODE_RATE := 48000


func _tone(rate: int, channels: int, seconds: float, hz := 440.0) -> PackedFloat32Array:
	var frames := int(rate * seconds)
	var pcm := PackedFloat32Array()
	pcm.resize(frames * channels)
	for i in frames:
		var value := 0.5 * sin(TAU * hz * i / rate)
		for c in channels:
			pcm[i * channels + c] = value
	return pcm


func _encoder(rate: int, channels: int, ms: int) -> GodotOpusEncoder:
	var encoder := GodotOpusEncoder.new()
	encoder.initialize(rate, channels, ms)
	# A steady tone isn't speech; keep Opus from switching to DTX on it.
	encoder.set_dtx(false)
	return encoder


func _decoder(channels: int, ms: int) -> GodotOpusDecoder:
	var decoder := GodotOpusDecoder.new()
	decoder.initialize(DECODE_RATE, channels, ms)
	return decoder


func test_classes_are_registered() -> void:
	for cls in [&"GodotOpusEncoder", &"GodotOpusDecoder", &"GodotOpusResampler"]:
		(
			assert_bool(ClassDB.class_exists(cls))
			. override_failure_message("%s missing" % cls)
			. is_true()
		)


func test_round_trip_decodes_at_48k(
	rate: int,
	ms: int,
	test_parameters := [[8000, 20], [16000, 10], [24000, 40], [48000, 5], [48000, 60]]
) -> void:
	var encoder := _encoder(rate, 1, ms)
	var decoder := _decoder(1, ms)
	var frame := encoder.get_frame_size()
	assert_int(frame).is_equal(rate * ms / 1000)

	var input := _tone(rate, 1, 0.3)
	var decoded_frames := 0
	for offset in range(0, input.size() - frame + 1, frame):
		var packet := encoder.encode(input.slice(offset, offset + frame))
		assert_int(packet.size()).is_greater(2)
		var pcm := decoder.decode(packet)
		assert_int(pcm.size()).is_equal(DECODE_RATE * ms / 1000)
		decoded_frames += 1
	assert_int(decoded_frames).is_greater(0)


func test_decode_sizes_output_from_the_packet() -> void:
	# Sender uses 60 ms frames; the receiver was configured for 20 ms. The
	# packet decodes in full instead of failing with "buffer too small".
	var encoder := _encoder(16000, 1, 60)
	var decoder := _decoder(1, 20)
	var input := _tone(16000, 1, 0.06)
	var pcm := decoder.decode(encoder.encode(input))
	assert_int(pcm.size()).is_equal(2880)


func test_stereo_output_is_interleaved() -> void:
	var encoder := _encoder(48000, 2, 20)
	var decoder := _decoder(2, 20)
	var pcm := decoder.decode(encoder.encode(_tone(48000, 2, 0.02)))
	assert_int(pcm.size()).is_equal(960 * 2)


func test_concealment_uses_the_frame_size() -> void:
	var decoder := _decoder(1, 20)
	assert_int(decoder.decode_plc().size()).is_equal(960)
	# An empty packet means "lost" and is concealed too.
	assert_int(decoder.decode(PackedByteArray()).size()).is_equal(960)
	assert_int(decoder.decode_fec(PackedByteArray()).size()).is_equal(960)

	decoder.set_frame_size(480)
	assert_int(decoder.decode_plc().size()).is_equal(480)


func test_invalid_frame_sizes_are_rejected() -> void:
	var encoder := _encoder(48000, 1, 20)
	encoder.set_frame_size(1000)
	assert_int(encoder.get_frame_size()).is_equal(960)
	encoder.set_frame_size(2880)
	assert_int(encoder.get_frame_size()).is_equal(2880)

	var decoder := _decoder(1, 20)
	decoder.set_frame_size(0)
	assert_int(decoder.get_frame_size()).is_equal(960)


func test_wrong_input_size_produces_no_packet() -> void:
	var encoder := _encoder(48000, 1, 20)
	assert_int(encoder.encode(PackedFloat32Array([0.0, 0.0])).size()).is_equal(0)


func test_silence_is_dtx() -> void:
	var encoder := GodotOpusEncoder.new()
	encoder.initialize(48000, 1, 20)  # DTX on by default
	var silence := PackedFloat32Array()
	silence.resize(960)
	var last := PackedByteArray()
	for i in 60:
		last = encoder.encode(silence)
	assert_int(last.size()).is_less_equal(2)
	assert_bool(encoder.is_in_dtx()).is_true()


func test_parse_dred_reports_available_samples() -> void:
	var encoder := _encoder(48000, 1, 20)
	var decoder := _decoder(1, 20)
	var input := _tone(48000, 1, 1.0, 220.0)

	if not encoder.has_dred_support():
		var packet := encoder.encode(input.slice(0, 960))
		assert_int(decoder.parse_dred(packet)).is_equal(0)
		assert_int(decoder.decode_dred(960).size()).is_equal(0)
		return

	encoder.set_dred_duration_ms(200)
	encoder.set_expected_packet_loss(30)
	encoder.set_bitrate(64000)
	var best := 0
	for offset in range(0, input.size() - 960 + 1, 960):
		best = maxi(best, decoder.parse_dred(encoder.encode(input.slice(offset, offset + 960))))
	assert_int(best).is_greater_equal(960)
	assert_int(decoder.decode_dred(960).size()).is_equal(960)
