#include "resampler.hpp"

namespace godotopus {

Resampler::~Resampler() {
	release();
}

void Resampler::release() {
	if (state) {
		speex_resampler_destroy(state);
		state = nullptr;
	}
	from_rate = 0;
	to_rate = 0;
	channels = 0;
}

int Resampler::init(int p_from_rate, int p_to_rate, int p_channels, int p_quality) {
	release();
	if (p_from_rate <= 0 || p_to_rate <= 0 || p_channels <= 0) {
		return RESAMPLER_ERR_INVALID_ARG;
	}

	int error = RESAMPLER_ERR_SUCCESS;
	state = speex_resampler_init(p_channels, p_from_rate, p_to_rate, p_quality, &error);
	if (error != RESAMPLER_ERR_SUCCESS) {
		state = nullptr;
		return error;
	}
	from_rate = p_from_rate;
	to_rate = p_to_rate;
	channels = p_channels;
	// Skip the filter's zero-filled warm-up so output starts with the signal.
	speex_resampler_skip_zeros(state);
	return RESAMPLER_ERR_SUCCESS;
}

int Resampler::process(const float *p_input, size_t p_frames, std::vector<float> &r_output) {
	r_output.clear();
	if (!state) {
		return RESAMPLER_ERR_INVALID_ARG;
	}
	if (p_frames == 0) {
		return RESAMPLER_ERR_SUCCESS;
	}
	if (!p_input) {
		return RESAMPLER_ERR_INVALID_ARG;
	}

	// A single call can stop early when the output estimate is too small, so
	// keep going until every input frame has been consumed.
	const float *input = p_input;
	size_t remaining = p_frames;
	while (remaining > 0) {
		spx_uint32_t in_frames = static_cast<spx_uint32_t>(remaining);
		spx_uint32_t out_frames = static_cast<spx_uint32_t>(
				static_cast<unsigned long long>(remaining) * to_rate / from_rate + 16);

		const size_t offset = r_output.size();
		r_output.resize(offset + static_cast<size_t>(out_frames) * channels);
		const int error = speex_resampler_process_interleaved_float(
				state, input, &in_frames, r_output.data() + offset, &out_frames);
		r_output.resize(offset + static_cast<size_t>(out_frames) * channels);
		if (error != RESAMPLER_ERR_SUCCESS) {
			r_output.clear();
			return error;
		}
		if (in_frames == 0 && out_frames == 0) {
			break; // No progress; avoid spinning.
		}
		input += static_cast<size_t>(in_frames) * channels;
		remaining -= in_frames;
	}
	return RESAMPLER_ERR_SUCCESS;
}

void Resampler::reset() {
	if (state) {
		speex_resampler_reset_mem(state);
		speex_resampler_skip_zeros(state);
	}
}

const char *Resampler::error_string(int p_error) {
	return speex_resampler_strerror(p_error);
}

} // namespace godotopus
