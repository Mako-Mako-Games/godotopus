#include "core/encoder.hpp"
#include "core/frame.hpp"
#include "core/voice_source.hpp"
#include "signals.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace godotopus;

namespace {

constexpr int TICK = 960; // the simulated audio clock: 20 ms at 48 kHz
constexpr double TONE_HZ = 440.0;
constexpr float AMPLITUDE = 0.5f;

using Packet = std::vector<uint8_t>;

struct EncodeOptions {
	int rate = 16000;
	int channels = 1;
	int frame_ms = 20;
	double seconds = 2.0;
	int bitrate = 24000;
	int dred_ms = 0; // ignored on light builds
	int expected_loss = 10;
};

// A tone encoded the way a sender would, minus DTX (a steady tone can be
// classified as non-speech).
std::vector<Packet> encode_tone(const EncodeOptions &p_options) {
	Encoder encoder;
	REQUIRE(encoder.init(p_options.rate, p_options.channels) == OPUS_OK);
	REQUIRE(encoder.set_dtx(false) == OPUS_OK);
	REQUIRE(encoder.set_bitrate(p_options.bitrate) == OPUS_OK);
	REQUIRE(encoder.set_expected_packet_loss(p_options.expected_loss) == OPUS_OK);
	if (p_options.dred_ms > 0 && encoder.supports_dred()) {
		REQUIRE(encoder.set_dred_duration_ms(p_options.dred_ms) == OPUS_OK);
	}

	const auto input = test_signals::sine(p_options.rate, p_options.channels, TONE_HZ, AMPLITUDE, p_options.seconds);
	const int samples = frame_samples(p_options.rate, p_options.frame_ms);
	const size_t step = static_cast<size_t>(samples) * p_options.channels;
	std::vector<Packet> packets;
	Packet packet;
	for (size_t offset = 0; offset + step <= input.size(); offset += step) {
		REQUIRE(encoder.encode(input.data() + offset, samples, packet) > 2);
		packets.push_back(packet);
	}
	return packets;
}

bool dred_supported() {
	Encoder encoder;
	return encoder.init(48000, 1) == OPUS_OK && encoder.supports_dred();
}

// Mixes p_frames and appends the stereo output to r_out.
void mix(VoiceSource &p_source, VoiceSource::Cursor &r_cursor, int p_frames, std::vector<float> &r_out) {
	const size_t offset = r_out.size();
	r_out.resize(offset + static_cast<size_t>(p_frames) * 2);
	p_source.mix(r_cursor, r_out.data() + offset, p_frames);
}

void push(VoiceSource &p_source, uint16_t p_seq, const Packet &p_packet) {
	p_source.push_packet(p_seq, p_packet.data(), p_packet.size());
}

// Delivers packet i at tick i (unless p_lost says otherwise) and mixes one
// tick after each delivery, plus p_tail ticks at the end. Returns the
// stereo output.
template <typename Lost>
std::vector<float> stream(VoiceSource &p_source, const std::vector<Packet> &p_packets, Lost p_lost, int p_tail = 10) {
	VoiceSource::Cursor cursor = p_source.live_cursor();
	std::vector<float> out;
	for (size_t i = 0; i < p_packets.size(); i++) {
		if (!p_lost(i)) {
			push(p_source, static_cast<uint16_t>(i), p_packets[i]);
		}
		mix(p_source, cursor, TICK, out);
	}
	for (int i = 0; i < p_tail; i++) {
		mix(p_source, cursor, TICK, out);
	}
	return out;
}

std::vector<float> stream(VoiceSource &p_source, const std::vector<Packet> &p_packets) {
	return stream(p_source, p_packets, [](size_t) { return false; });
}

// Smallest RMS over 20 ms windows in [p_from, p_to) frames of channel 0.
double min_window_rms(const std::vector<float> &p_stereo, size_t p_from, size_t p_to) {
	double lowest = 1e9;
	for (size_t start = p_from; start + TICK <= p_to; start += TICK) {
		const std::vector<float> window(p_stereo.begin() + start * 2, p_stereo.begin() + (start + TICK) * 2);
		lowest = std::min(lowest, test_signals::channel_rms(window, 2, 0));
	}
	return lowest;
}

} // namespace

TEST_CASE("voice source plays a steady stream at 48 kHz") {
	VoiceSource source;
	const auto packets = encode_tone({});
	const auto out = stream(source, packets);

	CHECK(out.size() == (packets.size() + 10) * TICK * 2);
	CHECK(test_signals::estimate_frequency(out, DECODE_SAMPLE_RATE, 2, DECODE_SAMPLE_RATE / 5) == doctest::Approx(TONE_HZ).epsilon(0.01));
	// Left and right carry the same mono voice.
	for (size_t i = 0; i < out.size(); i += 2) {
		REQUIRE(out[i] == out[i + 1]);
	}

	const VoiceSource::Stats stats = source.get_stats();
	CHECK(stats.jitter.packets_played == packets.size());
	CHECK(stats.jitter.frames_concealed == 0);
	CHECK(stats.jitter.underruns == 1); // only at the end
	CHECK(stats.decode_errors == 0);
}

TEST_CASE("latency is the target depth, and doesn't drift") {
	VoiceSource source; // 60 ms target
	const auto packets = encode_tone({});
	VoiceSource::Cursor cursor = source.live_cursor();
	std::vector<float> out;
	for (size_t i = 0; i < packets.size(); i++) {
		push(source, static_cast<uint16_t>(i), packets[i]);
		mix(source, cursor, TICK, out);
		if (i >= 10) {
			CAPTURE(i);
			// Delivered i + 1 frames, played i - 1 (two ticks of priming).
			REQUIRE(source.get_stats().jitter.depth_samples == 2 * TICK);
		}
	}
}

TEST_CASE("single losses are concealed without gaps in the audio") {
	VoiceSource source;
	const auto packets = encode_tone({});
	const auto lost = [](size_t i) { return i > 10 && i % 7 == 0; };
	size_t lost_count = 0;
	for (size_t i = 0; i < packets.size(); i++) {
		lost_count += lost(i) ? 1 : 0;
	}
	const auto out = stream(source, packets, lost);

	const VoiceSource::Stats stats = source.get_stats();
	CHECK(stats.jitter.frames_concealed == lost_count);
	CHECK(stats.frames_dred + stats.frames_fec + stats.frames_plc == lost_count);
	if (!dred_supported()) {
		CHECK(stats.frames_fec == lost_count);
	}
	// Audible all the way through: from after priming to the last packet.
	const size_t start = 5 * TICK;
	const size_t end = (packets.size() + 2) * TICK;
	CHECK(min_window_rms(out, start, end) > AMPLITUDE / std::sqrt(2.0) * 0.3);
	CHECK(test_signals::estimate_frequency(out, DECODE_SAMPLE_RATE, 2, DECODE_SAMPLE_RATE / 5) == doctest::Approx(TONE_HZ).epsilon(0.02));
}

TEST_CASE("burst loss uses DRED when the build has it, else FEC then PLC") {
	VoiceSource::Settings settings;
	// Deep enough that the packets after the gap arrive before the buffer
	// runs dry, so the gap is concealed rather than skipped.
	settings.jitter.target_samples = 5 * TICK;
	VoiceSource source;
	source.configure(settings);

	EncodeOptions options;
	options.rate = 48000;
	options.bitrate = 64000;
	options.dred_ms = 200;
	options.expected_loss = 30;
	const auto packets = encode_tone(options);
	stream(source, packets, [](size_t i) { return i >= 50 && i < 53; });

	const VoiceSource::Stats stats = source.get_stats();
	CHECK(stats.jitter.frames_concealed == 3);
	if (dred_supported()) {
		CHECK(stats.frames_dred == 3);
	} else {
		CHECK(stats.frames_dred == 0);
		CHECK(stats.frames_fec == 1); // the frame right before the next packet
		CHECK(stats.frames_plc == 2);
	}
}

TEST_CASE("network jitter and reordering within the target depth cost nothing") {
	VoiceSource source; // 60 ms target
	const auto packets = encode_tone({});
	// Each packet is delayed 0-2 ticks (pseudo-randomly), so they arrive
	// out of order and in clumps.
	std::vector<std::vector<size_t>> arrivals(packets.size() + 3);
	uint32_t state = 12345;
	for (size_t i = 0; i < packets.size(); i++) {
		state = state * 1664525u + 1013904223u;
		arrivals[i + (state >> 16) % 3].push_back(i);
	}

	VoiceSource::Cursor cursor = source.live_cursor();
	std::vector<float> out;
	for (const auto &tick : arrivals) {
		for (auto it = tick.rbegin(); it != tick.rend(); ++it) { // newest first
			push(source, static_cast<uint16_t>(*it), packets[*it]);
		}
		mix(source, cursor, TICK, out);
	}
	for (int i = 0; i < 5; i++) {
		mix(source, cursor, TICK, out);
	}

	const VoiceSource::Stats stats = source.get_stats();
	CHECK(stats.jitter.packets_played == packets.size());
	CHECK(stats.jitter.packets_late == 0);
	CHECK(stats.jitter.frames_concealed == 0);
	CHECK(test_signals::estimate_frequency(out, DECODE_SAMPLE_RATE, 2, DECODE_SAMPLE_RATE / 5) == doctest::Approx(TONE_HZ).epsilon(0.01));
}

TEST_CASE("the sender may change frame duration and sample rate mid-stream") {
	VoiceSource source;
	EncodeOptions first;
	first.seconds = 1.0;
	EncodeOptions second = first;
	second.rate = 24000;
	second.frame_ms = 60;
	const auto a = encode_tone(first);
	const auto b = encode_tone(second);

	VoiceSource::Cursor cursor = source.live_cursor();
	std::vector<float> out;
	uint16_t seq = 0;
	for (const Packet &packet : a) {
		push(source, seq++, packet);
		mix(source, cursor, TICK, out);
	}
	for (const Packet &packet : b) {
		push(source, seq++, packet);
		for (int i = 0; i < 3; i++) { // 60 ms per packet
			mix(source, cursor, TICK, out);
		}
	}
	for (int i = 0; i < 5; i++) {
		mix(source, cursor, TICK, out);
	}

	const VoiceSource::Stats stats = source.get_stats();
	CHECK(stats.jitter.packets_played == a.size() + b.size());
	CHECK(stats.jitter.frames_concealed == 0);
	CHECK(stats.decode_errors == 0);
	CHECK(test_signals::estimate_frequency(out, DECODE_SAMPLE_RATE, 2, DECODE_SAMPLE_RATE / 5) == doctest::Approx(TONE_HZ).epsilon(0.01));
}

TEST_CASE("several readers share one voice") {
	VoiceSource source;
	const auto packets = encode_tone({});
	VoiceSource::Cursor a = source.live_cursor();
	VoiceSource::Cursor b = source.live_cursor();
	std::vector<float> out_a;
	std::vector<float> out_b;

	SUBCASE("in step, in any chunk size") {
		for (size_t i = 0; i < packets.size(); i++) {
			push(source, static_cast<uint16_t>(i), packets[i]);
			mix(source, a, TICK / 2, out_a);
			mix(source, a, TICK / 2, out_a);
			mix(source, b, TICK, out_b);
		}
		CHECK(out_a == out_b);
		// The last two are still buffered at the 60 ms target depth.
		CHECK(source.get_stats().jitter.packets_played == packets.size() - 2);
	}

	SUBCASE("a reader joining later hears the voice live") {
		std::vector<float> late;
		VoiceSource::Cursor c = 0;
		for (size_t i = 0; i < packets.size(); i++) {
			push(source, static_cast<uint16_t>(i), packets[i]);
			if (i == 40) {
				c = source.live_cursor();
			}
			mix(source, a, TICK, out_a);
			if (i >= 40) {
				mix(source, c, TICK, late);
			}
		}
		const std::vector<float> tail(out_a.end() - late.size(), out_a.end());
		CHECK(late == tail);
	}

	SUBCASE("a reader that stopped mixing skips ahead instead of replaying old audio") {
		for (size_t i = 0; i < packets.size(); i++) {
			push(source, static_cast<uint16_t>(i), packets[i]);
			if (i < 20 || i >= 30) {
				mix(source, b, TICK, out_b);
			}
			mix(source, a, TICK, out_a);
		}
		// b fell 200 ms behind, more than MAX_READER_LAG, so it rejoined at
		// the live position and plays exactly what a plays from then on.
		const std::vector<float> b_after(out_b.begin() + 20 * TICK * 2, out_b.end());
		const std::vector<float> a_after(out_a.end() - b_after.size(), out_a.end());
		CHECK(b_after == a_after);
	}
}

TEST_CASE("a stereo decoder plays mono-coded packets on both channels") {
	VoiceSource::Settings settings;
	settings.channels = 2;
	VoiceSource source;
	source.configure(settings);
	EncodeOptions options;
	options.rate = 48000;
	options.channels = 2; // identical channels: Opus may code them as mono
	const auto out = stream(source, encode_tone(options));
	for (int channel : { 0, 1 }) {
		CAPTURE(channel);
		CHECK(test_signals::estimate_frequency(out, DECODE_SAMPLE_RATE, 2, DECODE_SAMPLE_RATE / 5, channel) == doctest::Approx(TONE_HZ).epsilon(0.01));
	}
}

TEST_CASE("speaking and level follow the stream") {
	VoiceSource source;
	const auto packets = encode_tone({});
	VoiceSource::Cursor cursor = source.live_cursor();
	std::vector<float> out;
	CHECK_FALSE(source.is_speaking());

	for (size_t i = 0; i < 20; i++) {
		push(source, static_cast<uint16_t>(i), packets[i]);
		mix(source, cursor, TICK, out);
	}
	CHECK(source.is_speaking());
	CHECK(source.get_level() > 0.1f);

	// Silence for longer than the 200 ms speaking timeout.
	for (int i = 0; i < 15; i++) {
		mix(source, cursor, TICK, out);
	}
	CHECK_FALSE(source.is_speaking());
	CHECK(source.get_level() == 0.0f);
}

TEST_CASE("malformed packets are counted and don't break the stream") {
	VoiceSource source;
	const uint8_t truncated[] = { 0x03 };
	source.push_packet(0, truncated, sizeof(truncated));
	source.push_packet(1, nullptr, 0);
	CHECK(source.get_stats().decode_errors == 2);
	CHECK(source.get_stats().jitter.packets_received == 0);

	const auto packets = encode_tone({});
	stream(source, packets);
	CHECK(source.get_stats().jitter.packets_played == packets.size());
}

TEST_CASE("reset drops the stream") {
	VoiceSource source;
	const auto packets = encode_tone({});
	for (uint16_t i = 0; i < 5; i++) {
		push(source, i, packets[i]);
	}
	source.reset();
	CHECK(source.get_stats().jitter.depth_samples == 0);
	VoiceSource::Cursor cursor = source.live_cursor();
	std::vector<float> out;
	mix(source, cursor, TICK, out);
	CHECK(test_signals::channel_rms(out, 2, 0) == 0.0);
}

// Run under ThreadSanitizer in CI (sanitize=thread).
TEST_CASE("packets can arrive on one thread while another mixes") {
	VoiceSource source;
	const auto packets = encode_tone({});
	constexpr int TOTAL = 2000;
	std::atomic<bool> producing{ true };

	std::thread producer([&]() {
		for (int i = 0; i < TOTAL; i++) {
			push(source, static_cast<uint16_t>(i), packets[i % packets.size()]);
			if (i % 8 == 0) {
				std::this_thread::yield();
			}
		}
		producing = false;
	});
	std::thread mixer([&]() {
		VoiceSource::Cursor cursor = source.live_cursor();
		std::vector<float> out(512 * 2);
		while (producing) {
			source.mix(cursor, out.data(), 512);
		}
	});
	std::thread observer([&]() {
		VoiceSource::Settings settings = source.get_settings();
		while (producing) {
			(void)source.get_stats();
			(void)source.get_level();
			(void)source.is_speaking();
			settings.complexity = settings.complexity == 0 ? 5 : 0;
			source.configure(settings);
			std::this_thread::yield();
		}
	});
	producer.join();
	mixer.join();
	observer.join();

	CHECK(source.get_stats().jitter.packets_received == TOTAL);
}

// Decoding runs inside the audio mixer, so it must stay far below real
// time, and no single mix may take long enough to threaten an audio
// callback. Complexity 10 is the worst case: on DNN builds it enables deep
// PLC and OSCE enhancement, and DRED decodes cost the most of all.
TEST_CASE("decode cost fits the audio deadline") {
	struct Case {
		int complexity;
		int loss_every; // 0 = no loss
	};
	for (const Case &c : { Case{ 0, 0 }, Case{ 5, 5 }, Case{ 10, 0 }, Case{ 10, 5 } }) {
		VoiceSource::Settings settings;
		settings.complexity = c.complexity;
		settings.jitter.target_samples = 5 * TICK;
		VoiceSource source;
		source.configure(settings);
		EncodeOptions options;
		options.rate = 48000;
		options.bitrate = 64000; // enough for DRED to be embedded
		options.dred_ms = 100;
		options.expected_loss = 20;
		options.seconds = 5.0;
		const auto packets = encode_tone(options);

		using Clock = std::chrono::steady_clock;
		VoiceSource::Cursor cursor = source.live_cursor();
		std::vector<float> out(TICK * 2);
		double total = 0.0;
		double worst = 0.0;
		for (size_t i = 0; i < packets.size(); i++) {
			if (!(c.loss_every > 0 && i > 10 && i % c.loss_every == 0)) {
				push(source, static_cast<uint16_t>(i), packets[i]);
			}
			const auto start = Clock::now();
			source.mix(cursor, out.data(), TICK);
			const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
			total += elapsed;
			worst = std::max(worst, elapsed);
		}
		const double load = total / (static_cast<double>(packets.size()) * TICK / DECODE_SAMPLE_RATE);
		const VoiceSource::Stats stats = source.get_stats();
		MESSAGE("complexity ", c.complexity, ", ", std::string(c.loss_every > 0 ? "20% loss" : "no loss"), ": ",
				load * 100.0, "% of real time, worst 20 ms mix ", worst * 1000.0, " ms (DRED ", stats.frames_dred,
				", FEC ", stats.frames_fec, ", PLC ", stats.frames_plc, ")");
		if (c.loss_every > 0 && dred_supported()) {
			CHECK(stats.frames_dred > 0);
		}
#ifndef GODOTOPUS_SANITIZED
		// Loose bounds for slow CI machines; the messages carry the numbers.
		CHECK(load < 0.10);
		CHECK(worst < 0.010);
#endif
	}
}
