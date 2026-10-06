#include "core/frame.hpp"

#include <doctest/doctest.h>

#include <initializer_list>

using namespace godotopus;

TEST_CASE("sample rates") {
	for (int rate : { 8000, 12000, 16000, 24000, 48000 }) {
		CHECK(is_valid_sample_rate(rate));
	}
	for (int rate : { 0, -48000, 11025, 22050, 44100, 96000 }) {
		CHECK_FALSE(is_valid_sample_rate(rate));
	}
}

TEST_CASE("frame durations") {
	for (int ms : { 5, 10, 20, 40, 60 }) {
		CHECK(is_valid_frame_duration_ms(ms));
	}
	for (int ms : { 0, 1, 2, 15, 25, 80, 120 }) {
		CHECK_FALSE(is_valid_frame_duration_ms(ms));
	}
	CHECK(frame_samples(48000, 20) == 960);
	CHECK(frame_samples(8000, 60) == 480);
	CHECK(frame_samples(12000, 5) == 60);
}

TEST_CASE("frame sizes Opus accepts") {
	// 2.5 ms through 120 ms.
	for (int samples : { 120, 240, 480, 960, 1920, 2880, 3840, 4800, 5760 }) {
		CHECK(is_valid_frame_samples(48000, samples));
	}
	CHECK(is_valid_frame_samples(8000, 20));
	CHECK(is_valid_frame_samples(16000, 320));
	for (int samples : { 0, -960, 100, 1000, 6240, 11520 }) {
		CHECK_FALSE(is_valid_frame_samples(48000, samples));
	}
	CHECK_FALSE(is_valid_frame_samples(44100, 882));
}
