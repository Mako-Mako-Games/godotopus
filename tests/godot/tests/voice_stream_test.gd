extends GdUnitTestSuite
## Tests for VoiceStream / VoiceStreamPlayback. Jitter buffering and
## concealment are covered by the native suite (tests/native); these check
## the engine integration: playbacks, resampling, players and stats.

const FRAME := 960  # 20 ms at 48 kHz
const TONE_HZ := 440.0


func _tone_packets(count: int, rate := 48000) -> Array[PackedByteArray]:
	var encoder := GodotOpusEncoder.new()
	encoder.initialize(rate, 1, 20)
	# A steady tone isn't speech; keep Opus from switching to DTX on it.
	encoder.set_dtx(false)
	var frame := encoder.get_frame_size()
	var packets: Array[PackedByteArray] = []
	var pcm := PackedFloat32Array()
	pcm.resize(frame)
	for p in count:
		for i in frame:
			pcm[i] = 0.5 * sin(TAU * TONE_HZ * (p * frame + i) / rate)
		packets.append(encoder.encode(pcm))
	return packets


## Frequency of the left channel from rising zero crossings.
func _frequency(frames: PackedVector2Array, rate: float, skip: int) -> float:
	var first := -1.0
	var last := -1.0
	var crossings := 0
	for i in range(skip + 1, frames.size()):
		var a := frames[i - 1].x
		var b := frames[i].x
		if a < 0.0 and b >= 0.0:
			var t := (i - 1) + a / (a - b)
			if first < 0.0:
				first = t
			last = t
			crossings += 1
	if crossings < 2:
		return 0.0
	return (crossings - 1) * rate / (last - first)


## Pushes one packet per 20 ms of output, mixing at the engine's mix rate.
func _stream(
	stream: VoiceStream, playback: AudioStreamPlayback, packets: Array[PackedByteArray]
) -> PackedVector2Array:
	var out := PackedVector2Array()
	var chunk := int(AudioServer.get_mix_rate() / 50.0)
	for i in packets.size():
		stream.push_packet(packets[i], i)
		out.append_array(playback.mix_audio(1.0, chunk))
	return out


func test_classes_are_registered() -> void:
	for cls in [&"VoiceStream", &"VoiceStreamPlayback"]:
		(
			assert_bool(ClassDB.class_exists(cls))
			. override_failure_message("%s missing" % cls)
			. is_true()
		)


func test_playback_decodes_and_resamples_to_the_mix_rate() -> void:
	var stream := VoiceStream.new()
	var playback := stream.instantiate_playback()
	assert_object(playback).is_instanceof(VoiceStreamPlayback)
	playback.start()
	assert_bool(playback.is_playing()).is_true()

	var out := _stream(stream, playback, _tone_packets(60))
	var rate := AudioServer.get_mix_rate()
	assert_float(_frequency(out, rate, int(rate * 0.2))).is_equal_approx(TONE_HZ, TONE_HZ * 0.01)

	var stats := stream.get_stats()
	assert_int(stats["packets_received"]).is_equal(60)
	assert_int(stats["packets_played"]).is_greater(50)
	assert_int(stats["frames_concealed"]).is_equal(0)
	assert_bool(stream.is_speaking()).is_true()
	assert_float(stream.get_level()).is_greater(0.1)

	playback.stop()
	assert_bool(playback.is_playing()).is_false()


func test_two_playbacks_share_one_voice() -> void:
	var stream := VoiceStream.new()
	var a := stream.instantiate_playback()
	var b := stream.instantiate_playback()
	a.start()
	b.start()
	var packets := _tone_packets(40)
	var out_a := PackedVector2Array()
	var out_b := PackedVector2Array()
	for i in packets.size():
		stream.push_packet(packets[i], i)
		out_a.append_array(a.mix_audio(1.0, 882))
		out_b.append_array(b.mix_audio(1.0, 882))

	var rate := AudioServer.get_mix_rate()
	for out in [out_a, out_b]:
		assert_float(_frequency(out, rate, int(rate * 0.2))).is_equal_approx(
			TONE_HZ, TONE_HZ * 0.01
		)
	# Both players read the same decoded audio rather than each consuming
	# packets, so nothing was skipped or concealed.
	var stats := stream.get_stats()
	assert_int(stats["packets_played"]).is_greater(35)
	assert_int(stats["frames_concealed"]).is_equal(0)
	assert_int(stats["packets_dropped"]).is_equal(0)


func test_lost_packets_are_concealed() -> void:
	var stream := VoiceStream.new()
	var playback := stream.instantiate_playback()
	playback.start()
	var packets := _tone_packets(60)
	var chunk := int(AudioServer.get_mix_rate() / 50.0)
	for i in packets.size():
		if i % 10 != 5:
			stream.push_packet(packets[i], i)
		playback.mix_audio(1.0, chunk)
	var stats := stream.get_stats()
	assert_int(stats["frames_concealed"]).is_equal(6)
	assert_int(stats["frames_dred"] + stats["frames_fec"] + stats["frames_plc"]).is_equal(6)


func test_silence_when_nothing_arrives() -> void:
	var stream := VoiceStream.new()
	var playback := stream.instantiate_playback()
	playback.start()
	var out := playback.mix_audio(1.0, 4410)
	assert_int(out.size()).is_equal(4410)
	for frame in out:
		assert_vector(frame).is_equal(Vector2.ZERO)
	assert_bool(stream.is_speaking()).is_false()
	assert_float(stream.get_level()).is_equal(0.0)


func test_audio_stream_player_pulls_audio_in_real_time() -> void:
	var stream := VoiceStream.new()
	var player := AudioStreamPlayer.new()
	player.stream = stream
	add_child(player)
	player.play()

	var packets := _tone_packets(50)
	var start := Time.get_ticks_usec()
	var sent := 0
	while sent < packets.size():
		# Send whatever is due by now, like a network would.
		var due := mini(packets.size(), int((Time.get_ticks_usec() - start) / 20000.0) + 1)
		while sent < due:
			stream.push_packet(packets[sent], sent)
			sent += 1
		await get_tree().process_frame
	await get_tree().create_timer(0.3).timeout

	var stats := stream.get_stats()
	assert_int(stats["packets_played"]).is_greater(40)
	player.queue_free()


func test_stats_report_every_counter() -> void:
	var stats := VoiceStream.new().get_stats()
	for key in [
		"packets_received",
		"packets_played",
		"packets_late",
		"packets_dropped",
		"packets_skipped",
		"frames_concealed",
		"frames_dred",
		"frames_fec",
		"frames_plc",
		"underruns",
		"resyncs",
		"decode_errors",
		"depth_ms",
	]:
		assert_bool(stats.has(key)).override_failure_message("missing %s" % key).is_true()


func test_settings_are_validated() -> void:
	var stream := VoiceStream.new()
	assert_int(stream.target_latency_ms).is_equal(60)
	assert_int(stream.max_latency_ms).is_equal(200)
	stream.channels = 3
	assert_int(stream.channels).is_equal(1)
	stream.channels = 2
	assert_int(stream.channels).is_equal(2)
	stream.decoder_complexity = 20
	assert_int(stream.decoder_complexity).is_equal(10)
	# The ceiling can't go below the target.
	stream.target_latency_ms = 100
	stream.max_latency_ms = 40
	assert_int(stream.max_latency_ms).is_equal(100)


func test_malformed_packets_are_counted() -> void:
	var stream := VoiceStream.new()
	stream.push_packet(PackedByteArray([3]), 0)
	stream.push_packet(PackedByteArray(), 1)
	assert_int(stream.get_stats()["decode_errors"]).is_equal(2)
	assert_int(stream.get_stats()["packets_received"]).is_equal(0)


func test_sequence_numbers_wrap() -> void:
	var stream := VoiceStream.new()
	var playback := stream.instantiate_playback()
	playback.start()
	var packets := _tone_packets(10)
	for i in packets.size():
		stream.push_packet(packets[i], 65530 + i)  # wraps after 65535
		playback.mix_audio(1.0, 882)
	var stats := stream.get_stats()
	assert_int(stats["resyncs"]).is_equal(0)
	assert_int(stats["packets_late"]).is_equal(0)
	assert_int(stats["frames_concealed"]).is_equal(0)
