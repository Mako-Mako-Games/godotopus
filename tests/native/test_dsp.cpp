#include "core/dsp.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace godotopus;

TEST_CASE("downmix averages left and right") {
	const std::vector<float> stereo = { 1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f };
	std::vector<float> mono(3);
	dsp::downmix_to_mono(stereo.data(), 3, mono.data());
	CHECK(mono[0] == doctest::Approx(0.5f));
	CHECK(mono[1] == doctest::Approx(0.5f));
	CHECK(mono[2] == doctest::Approx(0.0f));
}

TEST_CASE("mono to stereo duplicates samples") {
	const std::vector<float> mono = { 0.25f, -0.75f };
	std::vector<float> stereo(4);
	dsp::mono_to_stereo(mono.data(), 2, stereo.data());
	CHECK(stereo == std::vector<float>{ 0.25f, 0.25f, -0.75f, -0.75f });
}

TEST_CASE("rms") {
	CHECK(dsp::rms(nullptr, 0) == 0.0f);
	const std::vector<float> square = { 0.5f, -0.5f, 0.5f, -0.5f };
	CHECK(dsp::rms(square.data(), square.size()) == doctest::Approx(0.5f));
}

TEST_CASE("fade-in ramps linearly across buffers") {
	dsp::FadeIn fade;
	fade.start(4);
	CHECK(fade.is_active());

	std::vector<float> first = { 1.0f, 1.0f, 1.0f, 1.0f }; // 2 stereo frames
	fade.apply(first.data(), 2, 2);
	CHECK(first == std::vector<float>{ 0.0f, 0.0f, 0.25f, 0.25f });

	std::vector<float> second = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	fade.apply(second.data(), 3, 2);
	CHECK(second == std::vector<float>{ 0.5f, 0.5f, 0.75f, 0.75f, 1.0f, 1.0f });
	CHECK_FALSE(fade.is_active());

	// Once finished, audio passes through untouched.
	std::vector<float> third = { 0.3f };
	fade.apply(third.data(), 1, 1);
	CHECK(third[0] == 0.3f);

	fade.start(0);
	CHECK_FALSE(fade.is_active());
}
