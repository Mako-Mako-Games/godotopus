#include "core/capture_pipeline.hpp"
#include "core/decoder.hpp"
#include "core/frame.hpp"
#include "signals.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace godotopus;

namespace {

using Packet = CapturePipeline::Packet;

// Capture devices deliver stereo; signals.hpp makes mono or stereo.
std::vector<float> to_stereo(const std::vector<float> &p_mono) {
	std::vector<float> out(p_mono.size() * 2);
	for (size_t i = 0; i < p_mono.size(); i++) {
		out[i * 2] = p_mono[i];
		out[i * 2 + 1] = p_mono[i];
	}
	return out;
}

// Feeds p_stereo in chunks of p_chunk frames, like a game polling the mic
// once per frame, and collects every packet.
std::vector<Packet> feed(CapturePipeline &p_pipeline, const std::vector<float> &p_stereo, size_t p_chunk) {
	std::vector<Packet> packets;
	const size_t frames = p_stereo.size() / 2;
	for (size_t offset = 0; offset < frames; offset += p_chunk) {
		const size_t count = std::min(p_chunk, frames - offset);
		p_pipeline.push(p_stereo.data() + offset * 2, count, packets);
	}
	return packets;
}

std::vector<float> decode_all(const std::vector<Packet> &p_packets, int p_channels, std::vector<int> *r_durations = nullptr) {
	Decoder decoder;
	REQUIRE(decoder.init(DECODE_SAMPLE_RATE, p_channels) == OPUS_OK);
	std::vector<float> out;
	std::vector<float> pcm;
	for (const Packet &packet : p_packets) {
		const int samples = decoder.decode(packet.data(), packet.size(), pcm);
		REQUIRE(samples > 0);
		if (r_durations) {
			r_durations->push_back(samples);
		}
		out.insert(out.end(), pcm.begin(), pcm.end());
	}
	return out;
}

CapturePipeline::Settings tone_settings() {
	CapturePipeline::Settings settings;
	// A steady tone can be classified as non-speech; keep every frame.
	settings.dtx = false;
	return settings;
}

} // namespace

TEST_CASE("capture: encodes at every rate and frame duration, decodable at 48 kHz") {
	for (int rate : { 8000, 16000, 48000 }) {
		for (int ms : { 5, 20, 60 }) {
			CAPTURE(rate);
			CAPTURE(ms);
			CapturePipeline::Settings settings = tone_settings();
			settings.sample_rate = rate;
			settings.frame_duration_ms = ms;
			CapturePipeline pipeline;
			pipeline.configure(settings);

			const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 1.2));
			const auto packets = feed(pipeline, input, 800);
			// Every full frame comes out; the resampler holds back a few
			// samples of filter delay.
			const int expected = 1200 / ms;
			CHECK(static_cast<int>(packets.size()) >= expected - 1);
			CHECK(static_cast<int>(packets.size()) <= expected);

			std::vector<int> durations;
			const auto decoded = decode_all(packets, 1, &durations);
			for (int samples : durations) {
				REQUIRE(samples == frame_samples(DECODE_SAMPLE_RATE, ms));
			}
			CHECK(test_signals::estimate_frequency(decoded, DECODE_SAMPLE_RATE, 1, DECODE_SAMPLE_RATE / 5) == doctest::Approx(440.0).epsilon(0.01));
		}
	}
}

TEST_CASE("capture: resamples from the device rate") {
	for (int device_rate : { 44100, 96000 }) {
		CAPTURE(device_rate);
		CapturePipeline::Settings settings = tone_settings();
		settings.input_rate = device_rate;
		CapturePipeline pipeline;
		pipeline.configure(settings);

		const auto input = to_stereo(test_signals::sine(device_rate, 1, 440.0, 0.5f, 1.0));
		const auto packets = feed(pipeline, input, device_rate / 60);
		CHECK(packets.size() >= 49);
		CHECK(packets.size() <= 50);
		const auto decoded = decode_all(packets, 1);
		CHECK(test_signals::estimate_frequency(decoded, DECODE_SAMPLE_RATE, 1, DECODE_SAMPLE_RATE / 5) == doctest::Approx(440.0).epsilon(0.01));
	}
}

TEST_CASE("capture: output doesn't depend on how the input is chunked") {
	const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 1.0));
	size_t counts[3] = {};
	int i = 0;
	for (size_t chunk : { size_t(7), size_t(735), size_t(48000) }) {
		CapturePipeline::Settings settings = tone_settings();
		settings.max_backlog_ms = 1000;
		CapturePipeline pipeline;
		pipeline.configure(settings);
		counts[i++] = feed(pipeline, input, chunk).size();
	}
	CHECK(counts[0] == 50);
	CHECK(counts[1] == 50);
	CHECK(counts[2] == 50);
}

TEST_CASE("capture: stereo keeps both channels") {
	CapturePipeline::Settings settings = tone_settings();
	settings.channels = 2;
	// Enough bitrate for real stereo; at low rates Opus narrows the image.
	settings.bitrate = 96000;
	CapturePipeline pipeline;
	pipeline.configure(settings);

	// Different tones left and right, so a downmix would show.
	const auto left = test_signals::sine(48000, 1, 440.0, 0.5f, 0.6);
	const auto right = test_signals::sine(48000, 1, 660.0, 0.5f, 0.6);
	std::vector<float> input(left.size() * 2);
	for (size_t i = 0; i < left.size(); i++) {
		input[i * 2] = left[i];
		input[i * 2 + 1] = right[i];
	}
	const auto decoded = decode_all(feed(pipeline, input, 800), 2);
	CHECK(test_signals::estimate_frequency(decoded, 48000, 2, 9600, 0) == doctest::Approx(440.0).epsilon(0.01));
	CHECK(test_signals::estimate_frequency(decoded, 48000, 2, 9600, 1) == doctest::Approx(660.0).epsilon(0.01));
}

TEST_CASE("capture: silence sends nothing, including comfort-noise updates") {
	CapturePipeline pipeline; // DTX on by default
	const std::vector<float> silence(48000 * 2, 0.0f);
	// Opus needs a few frames to classify the input.
	feed(pipeline, std::vector<float>(silence.begin(), silence.begin() + 24000 * 2), 800);
	// Comfort-noise updates come every ~400 ms, so 3 s would contain several.
	std::vector<Packet> later;
	for (int i = 0; i < 3; i++) {
		const auto packets = feed(pipeline, silence, 800);
		later.insert(later.end(), packets.begin(), packets.end());
	}
	CHECK(later.empty());
	CHECK_FALSE(pipeline.is_speaking());
	CHECK(pipeline.get_stats().frames_silent >= 150);
	CHECK(pipeline.get_level() == 0.0f);
}

TEST_CASE("capture: speaking follows sent packets, with a timeout") {
	CapturePipeline::Settings settings;
	settings.speaking_timeout_ms = 200;
	CapturePipeline pipeline;
	pipeline.configure(settings);
	CHECK_FALSE(pipeline.is_speaking());

	const auto voice = to_stereo(test_signals::voice_like(48000, 1, 1.0));
	const auto spoken = feed(pipeline, voice, 960);
	CHECK(spoken.size() >= 45);
	CHECK(pipeline.is_speaking());
	CHECK(pipeline.get_level() > 0.05f);

	// Opus keeps sending for a short hangover after speech, then stops;
	// speaking ends once nothing was sent for the timeout.
	const std::vector<float> silence(960 * 2, 0.0f);
	std::vector<Packet> packets;
	int frames_until_quiet = -1;
	for (int frame = 0; frame < 100 && frames_until_quiet < 0; frame++) {
		pipeline.push(silence.data(), 960, packets);
		if (!pipeline.is_speaking()) {
			frames_until_quiet = frame + 1;
		}
	}
	REQUIRE(frames_until_quiet > 0);
	// At least the 200 ms timeout after the last packet; well under 2 s.
	CHECK(frames_until_quiet >= 10);
	CHECK(frames_until_quiet < 100);
}

TEST_CASE("capture: codec settings apply without restarting the encoder") {
	CapturePipeline pipeline;
	pipeline.configure(tone_settings());
	const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 0.5));

	size_t low_bytes = 0;
	for (const Packet &packet : feed(pipeline, input, 960)) {
		low_bytes += packet.size();
	}

	CapturePipeline::Settings settings = pipeline.get_settings();
	settings.bitrate = 64000;
	settings.complexity = 5;
	settings.expected_loss_percent = 30;
	settings.inband_fec = false;
	pipeline.configure(settings);
	size_t high_bytes = 0;
	for (const Packet &packet : feed(pipeline, input, 960)) {
		high_bytes += packet.size();
	}
	CHECK(high_bytes > low_bytes * 3 / 2);
	CHECK(pipeline.get_stats().encoder_restarts == 0);
}

TEST_CASE("capture: format changes restart the encoder once, on the next push") {
	CapturePipeline pipeline;
	pipeline.configure(tone_settings());
	const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 0.2));
	feed(pipeline, input, 960);

	CapturePipeline::Settings settings = pipeline.get_settings();
	settings.sample_rate = 16000;
	pipeline.configure(settings);
	settings.frame_duration_ms = 60;
	pipeline.configure(settings);
	settings.channels = 2;
	pipeline.configure(settings);
	CHECK(pipeline.get_stats().encoder_restarts == 0);

	const auto packets = feed(pipeline, to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 0.6)), 960);
	CHECK(pipeline.get_stats().encoder_restarts == 1);
	REQUIRE_FALSE(packets.empty());
	PacketInfo info;
	REQUIRE(inspect_packet(packets[0].data(), packets[0].size(), DECODE_SAMPLE_RATE, info) == OPUS_OK);
	CHECK(info.samples == 2880);

	// A new device rate only swaps the resampler.
	settings.input_rate = 44100;
	pipeline.configure(settings);
	feed(pipeline, to_stereo(test_signals::sine(44100, 1, 440.0, 0.5f, 0.2)), 735);
	CHECK(pipeline.get_stats().encoder_restarts == 1);
	CHECK(pipeline.get_stats().errors == 0);
}

TEST_CASE("capture: a stalled caller only sends the most recent audio") {
	CapturePipeline::Settings settings = tone_settings();
	settings.max_backlog_ms = 100;
	CapturePipeline pipeline;
	pipeline.configure(settings);

	// One second arrives at once, as after a hitch.
	const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 1.0));
	std::vector<Packet> packets;
	pipeline.push(input.data(), 48000, packets);
	CHECK(packets.size() == 5);
	CHECK(pipeline.get_stats().samples_dropped == 48000 - 4800);
}

TEST_CASE("capture: settings are validated") {
	CapturePipeline pipeline;
	CapturePipeline::Settings settings;
	settings.input_rate = 0;
	settings.sample_rate = 44100;
	settings.channels = 5;
	settings.frame_duration_ms = 25;
	settings.bitrate = 1;
	settings.complexity = 42;
	settings.expected_loss_percent = -3;
	settings.dred_duration_ms = 5000;
	settings.max_backlog_ms = 1;
	settings.speaking_timeout_ms = -1;
	pipeline.configure(settings);

	const auto &s = pipeline.get_settings();
	CHECK(s.input_rate == 8000);
	CHECK(s.sample_rate == 48000);
	CHECK(s.channels == 1);
	CHECK(s.frame_duration_ms == 20);
	CHECK(s.bitrate == 6000);
	CHECK(s.complexity == 10);
	CHECK(s.expected_loss_percent == 0);
	CHECK(s.dred_duration_ms == 1000);
	CHECK(s.max_backlog_ms == 20);
	CHECK(s.speaking_timeout_ms == 0);

	// Still encodes after snapping.
	std::vector<Packet> packets;
	const auto input = to_stereo(test_signals::sine(8000, 1, 440.0, 0.5f, 0.1));
	CHECK(pipeline.push(input.data(), 800, packets) >= 0);
	CHECK(pipeline.get_stats().errors == 0);
}

TEST_CASE("capture: reset drops buffered audio") {
	CapturePipeline pipeline;
	pipeline.configure(tone_settings());
	const auto input = to_stereo(test_signals::sine(48000, 1, 440.0, 0.5f, 0.1));
	std::vector<Packet> packets;
	pipeline.push(input.data(), 900, packets); // just under one frame
	CHECK(packets.empty());
	pipeline.reset();
	pipeline.push(input.data(), 900, packets); // the earlier 900 are gone
	CHECK(packets.empty());
	pipeline.push(input.data(), 60, packets);
	CHECK(packets.size() == 1);
}
