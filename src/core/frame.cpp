#include "frame.hpp"

namespace godotopus {

bool is_valid_sample_rate(int p_sample_rate) {
	switch (p_sample_rate) {
		case 8000:
		case 12000:
		case 16000:
		case 24000:
		case 48000:
			return true;
		default:
			return false;
	}
}

bool is_valid_frame_duration_ms(int p_duration_ms) {
	switch (p_duration_ms) {
		case 5:
		case 10:
		case 20:
		case 40:
		case 60:
			return true;
		default:
			return false;
	}
}

int frame_samples(int p_sample_rate, int p_duration_ms) {
	return p_sample_rate / 1000 * p_duration_ms;
}

bool is_valid_frame_samples(int p_sample_rate, int p_samples) {
	if (!is_valid_sample_rate(p_sample_rate) || p_samples <= 0) {
		return false;
	}
	// Durations in units of 2.5 ms, so they stay integral.
	static const int durations_x2[] = { 5, 10, 20, 40, 80, 120, 160, 200, 240 };
	for (int duration_x2 : durations_x2) {
		if (p_samples * 2000 == p_sample_rate * duration_x2) {
			return true;
		}
	}
	return false;
}

} // namespace godotopus
