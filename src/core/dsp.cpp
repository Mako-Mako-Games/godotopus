#include "dsp.hpp"

#include <cmath>

namespace godotopus::dsp {

void downmix_to_mono(const float *p_stereo, size_t p_frames, float *p_mono) {
	for (size_t i = 0; i < p_frames; i++) {
		p_mono[i] = (p_stereo[i * 2] + p_stereo[i * 2 + 1]) * 0.5f;
	}
}

void mono_to_stereo(const float *p_mono, size_t p_frames, float *p_stereo) {
	for (size_t i = 0; i < p_frames; i++) {
		p_stereo[i * 2] = p_mono[i];
		p_stereo[i * 2 + 1] = p_mono[i];
	}
}

float rms(const float *p_samples, size_t p_count) {
	if (p_count == 0) {
		return 0.0f;
	}
	double sum = 0.0;
	for (size_t i = 0; i < p_count; i++) {
		sum += static_cast<double>(p_samples[i]) * p_samples[i];
	}
	return static_cast<float>(std::sqrt(sum / p_count));
}

void FadeIn::start(int p_frames) {
	total = p_frames > 0 ? p_frames : 0;
	remaining = total;
}

void FadeIn::apply(float *p_interleaved, size_t p_frames, int p_channels) {
	for (size_t i = 0; i < p_frames && remaining > 0; i++, remaining--) {
		const float gain = 1.0f - static_cast<float>(remaining) / total;
		for (int c = 0; c < p_channels; c++) {
			p_interleaved[i * p_channels + c] *= gain;
		}
	}
}

} // namespace godotopus::dsp
