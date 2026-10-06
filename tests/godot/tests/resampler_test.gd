extends GdUnitTestSuite
## Tests for the GodotOpusResampler binding.


func _tone(rate: int, channels: int, frames: int) -> PackedFloat32Array:
	var pcm := PackedFloat32Array()
	pcm.resize(frames * channels)
	for i in frames:
		for c in channels:
			pcm[i * channels + c] = 0.5 * sin(TAU * 300.0 * i / rate)
	return pcm


func test_output_length_follows_the_ratio(
	from: int, to: int, channels: int, test_parameters := [[48000, 16000, 1], [44100, 48000, 2]]
) -> void:
	var resampler := GodotOpusResampler.new()
	resampler.initialize(from, to, channels)
	var total := 0
	for chunk in 10:
		total += resampler.resample(_tone(from, channels, from / 10)).size()
	var expected := float(from) * to / from * channels
	assert_float(float(total)).is_between(expected - 200.0 * channels, expected + channels)


func test_input_must_be_whole_frames() -> void:
	var resampler := GodotOpusResampler.new()
	resampler.initialize(48000, 16000, 2)
	assert_int(resampler.resample(PackedFloat32Array([0.0, 0.0, 0.0])).size()).is_equal(0)


func test_destroy_releases_the_resampler() -> void:
	var resampler := GodotOpusResampler.new()
	resampler.initialize(48000, 16000, 1)
	assert_int(resampler.get_channels()).is_equal(1)
	resampler.destroy()
	assert_int(resampler.get_channels()).is_equal(0)
	assert_int(resampler.resample(_tone(48000, 1, 480)).size()).is_equal(0)
