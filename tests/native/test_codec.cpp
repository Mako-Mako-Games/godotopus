#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "core/frame.hpp"
#include "signals.hpp"

#include <doctest/doctest.h>

#include <initializer_list>

#include <string>

using namespace godotopus;

namespace {

struct RoundTrip {
	std::vector<float> decoded; // at DECODE_SAMPLE_RATE
	std::vector<std::vector<uint8_t>> packets;
	std::vector<int> decoded_samples; // per packet, per channel
};

// Encodes p_input (interleaved, at p_rate) in p_duration_ms frames and
// decodes every packet at 48 kHz, the way a receiver always does.
// DTX is off: Opus's voice detector can classify a steady test tone as
// non-speech and replace it with comfort noise (see the DTX test).
RoundTrip round_trip(int p_rate, int p_channels, int p_duration_ms, const std::vector<float> &p_input) {
	Encoder encoder;
	REQUIRE(encoder.init(p_rate, p_channels) == OPUS_OK);
	REQUIRE(encoder.set_dtx(false) == OPUS_OK);
	Decoder decoder;
	REQUIRE(decoder.init(DECODE_SAMPLE_RATE, p_channels) == OPUS_OK);

	const int samples = frame_samples(p_rate, p_duration_ms);
	const size_t step = static_cast<size_t>(samples) * p_channels;
	RoundTrip result;
	std::vector<uint8_t> packet;
	std::vector<float> pcm;
	for (size_t offset = 0; offset + step <= p_input.size(); offset += step) {
		const int bytes = encoder.encode(p_input.data() + offset, samples, packet);
		REQUIRE(bytes > 0);
		result.packets.push_back(packet);

		const int decoded = decoder.decode(packet.data(), packet.size(), pcm);
		REQUIRE(decoded > 0);
		result.decoded_samples.push_back(decoded);
		result.decoded.insert(result.decoded.end(), pcm.begin(), pcm.end());
	}
	return result;
}

} // namespace

TEST_CASE("round trip keeps pitch, level and duration at every encode rate and frame duration") {
	const double tone_hz = 440.0;
	const float amplitude = 0.5f;
	const double input_rms = amplitude / std::sqrt(2.0);

	for (int rate : { 8000, 12000, 16000, 24000, 48000 }) {
		for (int ms : { 5, 10, 20, 40, 60 }) {
			CAPTURE(rate);
			CAPTURE(ms);
			const auto input = test_signals::sine(rate, 1, tone_hz, amplitude, 1.2);
			const RoundTrip result = round_trip(rate, 1, ms, input);

			// The receiver decodes at 48 kHz no matter what the sender used.
			const int expected = frame_samples(DECODE_SAMPLE_RATE, ms);
			for (int samples : result.decoded_samples) {
				REQUIRE(samples == expected);
			}

			const size_t warm_up = DECODE_SAMPLE_RATE / 5;
			const double hz = test_signals::estimate_frequency(result.decoded, DECODE_SAMPLE_RATE, 1, warm_up);
			CHECK(hz == doctest::Approx(tone_hz).epsilon(0.01));
			const double rms = test_signals::channel_rms(result.decoded, 1, warm_up);
			CHECK(rms > input_rms * 0.5);
			CHECK(rms < input_rms * 1.5);
		}
	}
}

TEST_CASE("stereo round trip keeps both channels") {
	const auto input = test_signals::sine(48000, 2, 440.0, 0.5f, 0.5);
	const RoundTrip result = round_trip(48000, 2, 20, input);
	CHECK(result.decoded.size() == result.decoded_samples.size() * 960 * 2);
	for (int channel : { 0, 1 }) {
		CAPTURE(channel);
		CHECK(test_signals::estimate_frequency(result.decoded, 48000, 2, 9600, channel) == doctest::Approx(440.0).epsilon(0.01));
	}
}

TEST_CASE("inspect_packet reads duration and channels from the packet") {
	for (int channels : { 1, 2 }) {
		for (int ms : { 5, 20, 60 }) {
			CAPTURE(channels);
			CAPTURE(ms);
			const auto input = test_signals::sine(16000, channels, 300.0, 0.5f, 0.2);
			const RoundTrip result = round_trip(16000, channels, ms, input);
			REQUIRE_FALSE(result.packets.empty());

			PacketInfo info;
			const auto &packet = result.packets.back();
			REQUIRE(inspect_packet(packet.data(), packet.size(), DECODE_SAMPLE_RATE, info) == OPUS_OK);
			CHECK(info.samples == frame_samples(DECODE_SAMPLE_RATE, ms));
			// A stereo encoder may code mono when that's cheaper (here both
			// channels are identical), so this is at most the encoder's count.
			CHECK(info.channels >= 1);
			CHECK(info.channels <= channels);
			CHECK(info.frames >= 1);
		}
	}

	PacketInfo info;
	const uint8_t truncated[] = { 0x03 }; // code 3 TOC without its frame count byte
	CHECK(inspect_packet(truncated, sizeof(truncated), 48000, info) < 0);
	CHECK(inspect_packet(nullptr, 0, 48000, info) == OPUS_BAD_ARG);
}

TEST_CASE("DTX: silence encodes to packets that don't need sending") {
	Encoder encoder;
	REQUIRE(encoder.init(48000, 1) == OPUS_OK); // DTX is on by default
	const std::vector<float> silence(960, 0.0f);
	std::vector<uint8_t> packet;

	int dtx_packets = 0;
	const int frames = 100;
	for (int i = 0; i < frames; i++) {
		const int bytes = encoder.encode(silence.data(), 960, packet);
		REQUIRE(bytes > 0);
		// Only judge the second half; Opus needs a moment to enter DTX.
		if (i >= frames / 2 && is_dtx_packet(bytes)) {
			dtx_packets++;
		}
	}
	CHECK(dtx_packets >= frames / 2 * 9 / 10);
	CHECK(encoder.is_in_dtx());

	// A real signal takes the encoder straight back out of DTX.
	const auto tone = test_signals::sine(48000, 1, 440.0, 0.5f, 0.1);
	int sent = 0;
	for (size_t offset = 0; offset + 960 <= tone.size(); offset += 960) {
		sent += is_dtx_packet(encoder.encode(tone.data() + offset, 960, packet)) ? 0 : 1;
	}
	CHECK(sent >= 4);
}

TEST_CASE("encoder rejects invalid configuration and input") {
	Encoder encoder;
	std::vector<uint8_t> packet;
	const std::vector<float> pcm(960, 0.0f);

	CHECK(encoder.encode(pcm.data(), 960, packet) == OPUS_INVALID_STATE);
	CHECK(encoder.init(44100, 1) == OPUS_BAD_ARG);
	CHECK_FALSE(encoder.is_initialized());
	CHECK(encoder.init(48000, 3) == OPUS_BAD_ARG);

	REQUIRE(encoder.init(48000, 1) == OPUS_OK);
	CHECK(encoder.encode(pcm.data(), 1000, packet) == OPUS_BAD_ARG);
	CHECK(encoder.encode(nullptr, 960, packet) == OPUS_BAD_ARG);
	CHECK(packet.empty());

	CHECK(encoder.set_bitrate(16000) == OPUS_OK);
	CHECK(encoder.get_bitrate() == 16000);
}

TEST_CASE("concealment and FEC produce the requested length") {
	const auto input = test_signals::sine(48000, 2, 440.0, 0.5f, 0.2);
	const RoundTrip result = round_trip(48000, 2, 20, input);

	Decoder decoder;
	REQUIRE(decoder.init(48000, 2) == OPUS_OK);
	std::vector<float> pcm;
	const auto &first = result.packets[0];
	REQUIRE(decoder.decode(first.data(), first.size(), pcm) == 960);
	CHECK(decoder.get_last_packet_samples() == 960);

	CHECK(decoder.conceal(960, pcm) == 960);
	CHECK(pcm.size() == 960 * 2);
	CHECK(decoder.conceal(480, pcm) == 480);
	CHECK(decoder.conceal(1000, pcm) == OPUS_BAD_ARG);
	CHECK(pcm.empty());

	const auto &next = result.packets[2];
	CHECK(decoder.decode_fec(next.data(), next.size(), 960, pcm) == 960);
	CHECK(pcm.size() == 960 * 2);
	// Without a packet, FEC falls back to concealment.
	CHECK(decoder.decode_fec(nullptr, 0, 960, pcm) == 960);
}

TEST_CASE("decoder survives malformed input") {
	Decoder decoder;
	std::vector<float> pcm;
	const uint8_t truncated[] = { 0x03 };
	CHECK(decoder.decode(truncated, 1, pcm) == OPUS_INVALID_STATE);

	REQUIRE(decoder.init(48000, 1) == OPUS_OK);
	CHECK(decoder.decode(truncated, 1, pcm) < 0);
	CHECK(pcm.empty());
	CHECK(decoder.decode(nullptr, 0, pcm) == OPUS_BAD_ARG);
}

TEST_CASE("DRED recovers history on DNN builds and is a clean no-op otherwise") {
	Encoder encoder;
	REQUIRE(encoder.init(48000, 1) == OPUS_OK);
	Decoder decoder;
	REQUIRE(decoder.init(48000, 1) == OPUS_OK);
	std::vector<uint8_t> packet;
	std::vector<float> pcm;
	const auto input = test_signals::sine(48000, 1, 220.0, 0.5f, 2.0);

	if (!encoder.supports_dred()) {
		MESSAGE("light build: DRED unavailable");
		CHECK(encoder.set_dred_duration_ms(200) == OPUS_UNIMPLEMENTED);
		REQUIRE(encoder.encode(input.data(), 960, packet) > 0);
		CHECK(decoder.parse_dred(packet.data(), packet.size()) == 0);
		CHECK(decoder.decode_dred(960, 960, pcm) == OPUS_BAD_ARG);
		return;
	}

	REQUIRE(encoder.set_dred_duration_ms(200) == OPUS_OK);
	CHECK(encoder.get_dred_duration_ms() == 200);
	// libopus funds DRED from (bitrate - ~12-20 kb/s) scaled by the expected
	// loss, so it needs both a decent bitrate and a nonzero loss estimate.
	REQUIRE(encoder.set_expected_packet_loss(30) == OPUS_OK);
	REQUIRE(encoder.set_bitrate(64000) == OPUS_OK);

	int best = 0;
	for (size_t offset = 0; offset + 960 <= input.size(); offset += 960) {
		REQUIRE(encoder.encode(input.data() + offset, 960, packet) > 0);
		const int available = decoder.parse_dred(packet.data(), packet.size());
		if (available >= best) {
			best = available;
			if (best >= 960) {
				// Recover the frame right before this packet.
				CHECK(decoder.decode_dred(960, 960, pcm) == 960);
				CHECK(pcm.size() == 960);
			}
		}
		REQUIRE(decoder.decode(packet.data(), packet.size(), pcm) > 0);
	}
	MESSAGE("DRED history available: ", best, " samples");
	CHECK(best >= 960);
	// Asking further back than the packet carries is rejected.
	CHECK(decoder.decode_dred(best + 960, 960, pcm) == OPUS_BAD_ARG);
}
