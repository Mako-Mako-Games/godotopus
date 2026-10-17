extends GdUnitTestSuite
## End-to-end tests for VoiceSender -> VoiceLoopbackTransport -> VoiceReceiver.
## The nodes don't process on their own here: each step captures, delivers
## and mixes 20 ms, so the tests don't depend on the frame rate.

const STEP := 0.02
const TONE_HZ := 440.0

var _mix_chunk := 0


class Rig:
	# Holds the source: a Callable doesn't keep its object alive.
	var source: TestSignals.Source
	var sender: VoiceSender
	var transport: VoiceLoopbackTransport
	var receiver: VoiceReceiver
	var playback: AudioStreamPlayback


func before_test() -> void:
	_mix_chunk = int(AudioServer.get_mix_rate() * STEP)


func _rig(
	source: TestSignals.Source,
	encoder: VoiceEncoderSettings = null,
	playback_settings: VoicePlaybackSettings = null
) -> Rig:
	var rig := Rig.new()
	rig.source = source
	rig.transport = auto_free(VoiceLoopbackTransport.new())
	rig.transport.random_seed = 7
	rig.sender = auto_free(VoiceSender.new())
	rig.sender.settings = encoder
	rig.sender.transport = rig.transport
	rig.sender.set_capture_source(source.read, source.rate)
	rig.receiver = auto_free(VoiceReceiver.new())
	rig.receiver.settings = playback_settings
	rig.receiver.transport = rig.transport
	for node in [rig.transport, rig.sender, rig.receiver]:
		node.process_mode = Node.PROCESS_MODE_DISABLED
		add_child(node)
	rig.playback = rig.receiver.get_stream().instantiate_playback()
	rig.playback.start()
	return rig


## Runs [param steps] 20 ms steps and returns the mixed output.
func _run(rig: Rig, steps: int) -> PackedVector2Array:
	var out := PackedVector2Array()
	for i in steps:
		rig.sender.poll()
		rig.transport.advance(STEP)
		out.append_array(rig.playback.mix_audio(1.0, _mix_chunk))
		rig.receiver.poll()
	return out


## A steady tone can be classified as non-speech; keep every frame.
func _no_dtx() -> VoiceEncoderSettings:
	var settings := VoiceEncoderSettings.new()
	settings.dtx = false
	return settings


func test_tone_survives_a_lossy_reordering_network() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(TONE_HZ), 48000, 1.0), _no_dtx())
	rig.transport.loss_percent = 10.0
	rig.transport.jitter_ms = 30  # more than a frame, so packets reorder
	var out := _run(rig, 80)

	var rate := AudioServer.get_mix_rate()
	var tone := out.slice(int(rate * 0.2), int(rate * 0.9))
	assert_float(TestSignals.median_frequency(tone, rate)).is_equal_approx(TONE_HZ, TONE_HZ * 0.01)
	assert_float(TestSignals.audible_seconds(out, rate)).is_between(0.9, 1.1)

	var stats := rig.receiver.get_stats()
	assert_int(rig.transport.packets_lost).is_greater(0)
	assert_int(stats["frames_concealed"]).is_greater(0)
	# With DTX off the silence after the tone is sent too: 80 packets.
	assert_int(stats["packets_played"] + stats["frames_concealed"]).is_between(72, 80)
	assert_int(stats["decode_errors"]).is_equal(0)


func test_silence_sends_no_packets() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(0.0, 0.0), 48000, 3.0))
	_run(rig, 25)  # Opus sends a few frames before it classifies the input
	var warmed_up := rig.transport.packets_sent
	_run(rig, 125)
	assert_int(rig.transport.packets_sent).is_equal(warmed_up)
	assert_int(rig.sender.get_stats()["frames_silent"]).is_greater(125)


func test_not_transmitting_sends_nothing() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(TONE_HZ), 48000, 1.0), _no_dtx())
	rig.sender.transmitting = false
	_run(rig, 50)
	assert_int(rig.transport.packets_sent).is_equal(0)
	assert_bool(rig.sender.is_speaking()).is_false()


func test_bitrate_changes_apply_without_a_restart() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(TONE_HZ), 48000, 2.0), _no_dtx())
	var out := _run(rig, 50)
	rig.sender.settings.bitrate = 64000
	rig.sender.settings.expected_packet_loss_percent = 30
	out.append_array(_run(rig, 60))

	assert_int(rig.sender.get_stats()["encoder_restarts"]).is_equal(0)
	var stats := rig.receiver.get_stats()
	assert_int(stats["frames_concealed"]).is_equal(0)
	assert_int(stats["resyncs"]).is_equal(0)
	assert_float(TestSignals.audible_seconds(out, AudioServer.get_mix_rate())).is_between(1.9, 2.1)


func test_frame_duration_changes_restart_only_the_sender() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(TONE_HZ), 48000, 2.0), _no_dtx())
	var stream := rig.receiver.get_stream()
	var out := _run(rig, 50)
	rig.sender.settings.frame_duration_ms = 60
	rig.sender.settings.sample_rate = 16000
	out.append_array(_run(rig, 60))

	assert_int(rig.sender.get_stats()["encoder_restarts"]).is_equal(1)
	# The receiver follows the new format from the packets alone.
	assert_object(rig.receiver.get_stream()).is_same(stream)
	var stats := rig.receiver.get_stats()
	assert_int(stats["decode_errors"]).is_equal(0)
	assert_int(stats["resyncs"]).is_equal(0)
	var rate := AudioServer.get_mix_rate()
	assert_float(TestSignals.audible_seconds(out, rate)).is_between(1.85, 2.1)
	var tail := out.slice(int(rate * 1.3), int(rate * 1.9))
	assert_float(TestSignals.frequency(tail, rate)).is_equal_approx(TONE_HZ, TONE_HZ * 0.01)


func test_presets_are_copied_on_assignment() -> void:
	var preset := VoiceEncoderSettings.new()
	preset.bitrate = 16000
	var a: VoiceSender = auto_free(VoiceSender.new())
	var b: VoiceSender = auto_free(VoiceSender.new())
	a.settings = preset
	b.settings = preset
	assert_object(a.settings).is_not_same(preset)
	assert_object(a.settings).is_not_same(b.settings)
	assert_int(a.settings.bitrate).is_equal(16000)

	a.settings.bitrate = 48000
	assert_int(b.settings.bitrate).is_equal(16000)
	assert_int(preset.bitrate).is_equal(16000)
	preset.bitrate = 8000
	assert_int(b.settings.bitrate).is_equal(16000)

	var playback := VoicePlaybackSettings.new()
	var r: VoiceReceiver = auto_free(VoiceReceiver.new())
	r.settings = playback
	r.settings.target_latency_ms = 120
	assert_int(playback.target_latency_ms).is_equal(60)
	assert_int(r.get_stream().target_latency_ms).is_equal(120)


func test_speaking_signals_follow_the_voice() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.voice_like(), 48000, 1.0))
	var events: Array[String] = []
	var step := [0]
	for node in [rig.sender, rig.receiver]:
		var who := "sender" if node == rig.sender else "receiver"
		node.speaking_started.connect(func(): events.append("%s started %d" % [who, step[0]]))
		node.speaking_ended.connect(func(): events.append("%s ended %d" % [who, step[0]]))
	for i in 150:
		step[0] = i
		_run(rig, 1)

	# One utterance: each side starts once near the beginning and ends once,
	# some time after the voice stops at step 50.
	assert_int(events.size()).override_failure_message(str(events)).is_equal(4)
	var started := events.filter(func(e): return "started" in e)
	var ended := events.filter(func(e): return "ended" in e)
	assert_int(started.size()).override_failure_message(str(events)).is_equal(2)
	assert_int(ended.size()).override_failure_message(str(events)).is_equal(2)
	for e in started:
		assert_int(int(e.get_slice(" ", 2))).override_failure_message(str(events)).is_less(10)
	for e in ended:
		assert_int(int(e.get_slice(" ", 2))).override_failure_message(str(events)).is_between(
			55, 100
		)


func test_receiver_ignores_other_peers() -> void:
	var rig := _rig(TestSignals.Source.new(TestSignals.tone(TONE_HZ), 48000, 0.5), _no_dtx())
	rig.receiver.peer_id = 2  # the loopback reports peer 1
	_run(rig, 25)
	assert_int(rig.transport.packets_sent).is_greater(20)
	assert_int(rig.receiver.get_stats()["packets_received"]).is_equal(0)


func test_receiver_assigns_and_starts_its_players() -> void:
	var a: AudioStreamPlayer = auto_free(AudioStreamPlayer.new())
	var b: AudioStreamPlayer3D = auto_free(AudioStreamPlayer3D.new())
	add_child(a)
	add_child(b)
	var receiver: VoiceReceiver = auto_free(VoiceReceiver.new())
	receiver.players = [a]
	add_child(receiver)
	assert_object(a.stream).is_same(receiver.get_stream())
	assert_bool(a.playing).is_true()

	receiver.add_player(b)
	assert_object(b.stream).is_same(receiver.get_stream())
	assert_bool(b.playing).is_true()

	receiver.remove_player(a)
	assert_object(a.stream).is_null()
	assert_bool(a.playing).is_false()
	assert_array(receiver.players).contains_exactly([b])
