#pragma once

// Test signal generators and measurements.

#include <cmath>
#include <cstddef>
#include <vector>

namespace test_signals {

constexpr double PI = 3.14159265358979323846;

// Interleaved sine on every channel.
inline std::vector<float> sine(int p_rate, int p_channels, double p_hz, float p_amplitude, double p_seconds) {
	const size_t frames = static_cast<size_t>(p_rate * p_seconds);
	std::vector<float> out(frames * p_channels);
	for (size_t i = 0; i < frames; i++) {
		const float value = p_amplitude * static_cast<float>(std::sin(2.0 * PI * p_hz * i / p_rate));
		for (int c = 0; c < p_channels; c++) {
			out[i * p_channels + c] = value;
		}
	}
	return out;
}

// Frequency of channel p_channel estimated from rising zero crossings,
// ignoring the first p_skip_frames frames (codec/filter warm-up).
inline double estimate_frequency(const std::vector<float> &p_samples, int p_rate, int p_channels, size_t p_skip_frames, int p_channel = 0) {
	const size_t frames = p_samples.size() / p_channels;
	double first = -1.0;
	double last = -1.0;
	int crossings = 0;
	for (size_t i = p_skip_frames + 1; i < frames; i++) {
		const float a = p_samples[(i - 1) * p_channels + p_channel];
		const float b = p_samples[i * p_channels + p_channel];
		if (a < 0.0f && b >= 0.0f) {
			// Interpolate the crossing position between the two samples.
			const double t = (i - 1) + a / static_cast<double>(a - b);
			if (first < 0.0) {
				first = t;
			}
			last = t;
			crossings++;
		}
	}
	if (crossings < 2) {
		return 0.0;
	}
	return (crossings - 1) * p_rate / (last - first);
}

// RMS of channel p_channel after skipping p_skip_frames frames.
inline double channel_rms(const std::vector<float> &p_samples, int p_channels, size_t p_skip_frames, int p_channel = 0) {
	const size_t frames = p_samples.size() / p_channels;
	if (frames <= p_skip_frames) {
		return 0.0;
	}
	double sum = 0.0;
	for (size_t i = p_skip_frames; i < frames; i++) {
		const double v = p_samples[i * p_channels + p_channel];
		sum += v * v;
	}
	return std::sqrt(sum / (frames - p_skip_frames));
}

} // namespace test_signals
