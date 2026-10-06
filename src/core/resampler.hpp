#pragma once

#include <cstddef>
#include <vector>

#include <speex/speex_resampler.h>

namespace godotopus {

// Streaming sample-rate converter (SpeexDSP) for interleaved float PCM.
// Methods returning int give RESAMPLER_ERR_SUCCESS (0) or a SpeexDSP error
// code (see error_string()).
class Resampler {
public:
	Resampler() = default;
	~Resampler();
	Resampler(const Resampler &) = delete;
	Resampler &operator=(const Resampler &) = delete;

	// p_quality is 0 (fastest) to 10 (best).
	int init(int p_from_rate, int p_to_rate, int p_channels, int p_quality = 5);
	bool is_initialized() const { return state != nullptr; }

	// Converts p_frames frames of interleaved input into r_output, consuming
	// all of it. Output length follows the rate ratio over time; individual
	// calls can differ by a few frames because of the filter delay.
	int process(const float *p_input, size_t p_frames, std::vector<float> &r_output);

	// Clears the filter history, e.g. between unrelated streams.
	void reset();

	// Frees the resampler; is_initialized() is false afterwards.
	void release();

	int get_from_rate() const { return from_rate; }
	int get_to_rate() const { return to_rate; }
	int get_channels() const { return channels; }

	static const char *error_string(int p_error);

private:
	SpeexResamplerState *state = nullptr;
	int from_rate = 0;
	int to_rate = 0;
	int channels = 0;
};

} // namespace godotopus
