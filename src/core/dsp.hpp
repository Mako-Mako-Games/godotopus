#pragma once

#include <cstddef>

// Small per-sample helpers for the capture and playback paths. Godot's audio
// frames are stereo pairs (Vector2), so they arrive as interleaved L/R floats.

namespace godotopus::dsp {

// Averages interleaved stereo frames into mono. p_mono must hold p_frames.
void downmix_to_mono(const float *p_stereo, size_t p_frames, float *p_mono);

// Duplicates mono samples into interleaved stereo. p_stereo must hold 2 * p_frames.
void mono_to_stereo(const float *p_mono, size_t p_frames, float *p_stereo);

// Root mean square of p_count samples; 0 for an empty buffer.
float rms(const float *p_samples, size_t p_count);

// Linear fade-in that can span several buffers, used to avoid a click when
// playback resumes after silence.
class FadeIn {
public:
	// Starts a fade lasting p_frames frames. 0 disables fading.
	void start(int p_frames);
	bool is_active() const { return remaining > 0; }

	// Scales the start of p_frames frames of interleaved audio in place.
	void apply(float *p_interleaved, size_t p_frames, int p_channels);

private:
	int total = 0;
	int remaining = 0;
};

} // namespace godotopus::dsp
