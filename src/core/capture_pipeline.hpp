#pragma once

#include "encoder.hpp"
#include "resampler.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace godotopus {

// The send side of one voice: takes stereo frames from the capture device,
// downmixes them to the encode channel count, resamples them to the encode
// rate, cuts them into frames and encodes them. Only packets worth sending
// come out; frames Opus classifies as silence (DTX) are dropped here, so
// sequence numbers assigned to the output count transmitted packets.
//
// Not thread-safe: drive it from one thread (Godot's main thread).
class CapturePipeline {
public:
	struct Settings {
		// Rate of the stereo frames passed to push().
		int input_rate = 48000;
		// Encode format. Changing any of these restarts the encoder.
		int sample_rate = 48000;
		int channels = 1;
		int frame_duration_ms = 20;
		// Applied to the running encoder.
		int bitrate = 24000;
		int complexity = 10;
		bool inband_fec = true;
		int expected_loss_percent = 10;
		bool dtx = true;
		int dred_duration_ms = 0; // ignored on builds without DNN support
		// Captured audio waiting to be encoded beyond this (after a hitch)
		// is dropped, oldest first, so listeners don't get a stale burst.
		int max_backlog_ms = 100;
		// How long after the last sent packet the voice still counts as
		// speaking.
		int speaking_timeout_ms = 200;
	};

	struct Stats {
		uint64_t frames_encoded = 0;
		uint64_t packets_sent = 0;
		uint64_t frames_silent = 0;	   // classified as silence and not sent
		uint64_t samples_dropped = 0;  // backlog trimmed, per channel at the encode rate
		uint64_t encoder_restarts = 0; // encode format changes
		uint64_t errors = 0;		   // failed encodes or resamples
	};

	using Packet = std::vector<uint8_t>;

	CapturePipeline();

	// Validates and stores p_settings. Codec options take effect right away;
	// a new encode format or input rate is applied on the next push(), so
	// several changes in a row cost one restart.
	void configure(const Settings &p_settings);
	const Settings &get_settings() const { return settings; }

	// Feeds p_frames interleaved stereo frames at the input rate and appends
	// every packet to send to r_packets. Returns how many were appended.
	int push(const float *p_stereo, size_t p_frames, std::vector<Packet> &r_packets);

	// Drops buffered audio and encoder state, e.g. when transmission pauses.
	void reset();

	bool is_speaking() const { return speaking; }
	// RMS of the audio from the last push() (after downmixing).
	float get_level() const { return level; }
	const Stats &get_stats() const { return stats; }

	// True if the encoder is built with Deep REDundancy support.
	bool supports_dred() const;

private:
	void apply_codec_settings();
	bool ensure_started();

	Settings settings;
	Stats stats;
	Encoder encoder;
	Resampler resampler; // only used when input_rate != sample_rate

	bool restart_pending = true;
	bool resampler_pending = true;
	int frame_size = 0; // samples per channel per frame

	std::vector<float> converted; // downmixed input
	std::vector<float> resampled; // converted, at the encode rate
	std::vector<float> pending;	  // encode-rate audio not yet framed
	Packet packet;

	float level = 0.0f;
	bool speaking = false;
	int silent_samples = 0; // per channel since the last sent packet
};

} // namespace godotopus
