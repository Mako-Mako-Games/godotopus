#include "core/resampler.hpp"
#include "signals.hpp"

#include <doctest/doctest.h>

#include <initializer_list>

#include <algorithm>
#include <cmath>

using namespace godotopus;

namespace {

std::vector<float> resample_in_chunks(Resampler &p_resampler, const std::vector<float> &p_input, size_t p_chunk_frames) {
	const int channels = p_resampler.get_channels();
	std::vector<float> all;
	std::vector<float> out;
	for (size_t offset = 0; offset < p_input.size(); offset += p_chunk_frames * channels) {
		const size_t frames = std::min(p_chunk_frames, (p_input.size() - offset) / channels);
		REQUIRE(p_resampler.process(p_input.data() + offset, frames, out) == RESAMPLER_ERR_SUCCESS);
		all.insert(all.end(), out.begin(), out.end());
	}
	return all;
}

} // namespace

TEST_CASE("output length follows the rate ratio") {
	struct Case {
		int from;
		int to;
	};
	for (Case c : { Case{ 48000, 16000 }, Case{ 44100, 48000 }, Case{ 48000, 8000 }, Case{ 16000, 48000 } }) {
		CAPTURE(c.from);
		CAPTURE(c.to);
		Resampler resampler;
		REQUIRE(resampler.init(c.from, c.to, 1) == RESAMPLER_ERR_SUCCESS);
		const auto input = test_signals::sine(c.from, 1, 300.0, 0.5f, 1.0);
		const auto output = resample_in_chunks(resampler, input, 441);

		// Skipping the filter warm-up drops a few dozen input frames' worth
		// of output, which scales with the conversion ratio.
		const double ratio = static_cast<double>(c.to) / c.from;
		const double expected = input.size() * ratio;
		CHECK(output.size() <= expected + 1);
		CHECK(output.size() >= expected - 64 * std::max(1.0, ratio));
	}
}

TEST_CASE("resampling keeps pitch") {
	Resampler resampler;
	REQUIRE(resampler.init(44100, 48000, 1) == RESAMPLER_ERR_SUCCESS);
	const auto input = test_signals::sine(44100, 1, 1000.0, 0.5f, 1.0);
	const auto output = resample_in_chunks(resampler, input, 512);
	CHECK(test_signals::estimate_frequency(output, 48000, 1, 480) == doctest::Approx(1000.0).epsilon(0.002));
	CHECK(test_signals::channel_rms(output, 1, 480) == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.05));
}

TEST_CASE("chunked and one-shot processing give the same stream") {
	const auto input = test_signals::sine(48000, 2, 440.0, 0.5f, 0.5);

	Resampler one_shot;
	REQUIRE(one_shot.init(48000, 16000, 2) == RESAMPLER_ERR_SUCCESS);
	std::vector<float> whole;
	REQUIRE(one_shot.process(input.data(), input.size() / 2, whole) == RESAMPLER_ERR_SUCCESS);

	Resampler chunked;
	REQUIRE(chunked.init(48000, 16000, 2) == RESAMPLER_ERR_SUCCESS);
	const auto pieces = resample_in_chunks(chunked, input, 333);

	REQUIRE(pieces.size() == whole.size());
	float max_difference = 0.0f;
	for (size_t i = 0; i < whole.size(); i++) {
		max_difference = std::max(max_difference, std::fabs(whole[i] - pieces[i]));
	}
	CHECK(max_difference < 1e-5f);
}

TEST_CASE("channels stay separate") {
	// Left carries a tone, right is silent.
	std::vector<float> input = test_signals::sine(48000, 2, 500.0, 0.5f, 0.5);
	for (size_t i = 1; i < input.size(); i += 2) {
		input[i] = 0.0f;
	}
	Resampler resampler;
	REQUIRE(resampler.init(48000, 24000, 2) == RESAMPLER_ERR_SUCCESS);
	std::vector<float> output;
	REQUIRE(resampler.process(input.data(), input.size() / 2, output) == RESAMPLER_ERR_SUCCESS);
	CHECK(test_signals::channel_rms(output, 2, 240, 0) > 0.3);
	CHECK(test_signals::channel_rms(output, 2, 240, 1) < 1e-4);
}

TEST_CASE("invalid use is reported, not crashed on") {
	Resampler resampler;
	std::vector<float> output;
	const float sample = 0.0f;
	CHECK(resampler.process(&sample, 1, output) != RESAMPLER_ERR_SUCCESS);
	CHECK(resampler.init(0, 48000, 1) != RESAMPLER_ERR_SUCCESS);
	CHECK(resampler.init(48000, 48000, 0) != RESAMPLER_ERR_SUCCESS);
	CHECK_FALSE(resampler.is_initialized());

	REQUIRE(resampler.init(48000, 16000, 1) == RESAMPLER_ERR_SUCCESS);
	CHECK(resampler.process(nullptr, 0, output) == RESAMPLER_ERR_SUCCESS);
	CHECK(output.empty());
	resampler.release();
	CHECK_FALSE(resampler.is_initialized());
	CHECK(resampler.get_channels() == 0);
}
