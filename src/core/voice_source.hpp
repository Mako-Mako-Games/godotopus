#pragma once

#include "decoder.hpp"
#include "dsp.hpp"
#include "jitter_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace godotopus {

// One remote voice: packets go in from any thread, and audio is decoded on
// demand by whoever mixes it (the audio thread). Output is always 48 kHz.
//
// Several readers can play the same voice at once (e.g. a dry player plus
// one on a radio-effect bus). Decoded audio goes into a shared ring and each
// reader keeps its own cursor into it; the reader furthest ahead drives
// decoding and the others replay what it produced.
class VoiceSource {
public:
	struct Settings {
		JitterBuffer::Settings jitter;
		// Decoder channels: 1 or 2. Not taken from the packets, since a
		// stereo encoder may code individual packets as mono.
		int channels = 1;
		// 0..10. On DNN builds, >= 5 enables deep PLC and higher values
		// enable OSCE enhancement.
		int complexity = 0;
		bool bandwidth_extension = false;
		// Fade-in after a discontinuity, in samples at 48 kHz.
		int fade_in_samples = 240; // 5 ms
		// How long after the last packet the voice still counts as speaking.
		int speaking_timeout_samples = 9600; // 200 ms
	};

	struct Stats {
		JitterBuffer::Stats jitter;
		uint64_t frames_dred = 0; // concealed from Deep REDundancy data
		uint64_t frames_fec = 0;  // concealed from in-band FEC data
		uint64_t frames_plc = 0;  // concealed by extrapolation
		uint64_t decode_errors = 0;
	};

	// Frames a reader may fall behind the newest audio before it skips ahead.
	static constexpr int MAX_READER_LAG = 4800; // 100 ms
	// Decoded history kept for lagging readers, in frames.
	static constexpr int RING_FRAMES = 16384;

	VoiceSource();

	// Applies new settings. Changing channels resets the stream.
	void configure(const Settings &p_settings);
	Settings get_settings() const;

	// Thread-safe. Malformed packets are counted as decode errors and dropped.
	void push_packet(uint16_t p_seq, const uint8_t *p_data, size_t p_size);

	// A reader's position in the decoded timeline. Start new readers at
	// live_cursor() so they hear the voice from now on.
	using Cursor = int64_t;
	Cursor live_cursor() const;

	// Writes p_frames interleaved stereo frames to p_out and advances
	// r_cursor. Plays silence when nothing is buffered. Called by readers,
	// normally on the audio thread.
	void mix(Cursor &r_cursor, float *p_out, int p_frames);

	// Drops buffered packets and decoder state.
	void reset();

	Stats get_stats() const;
	// RMS of the most recently decoded audio (0 during silence).
	float get_level() const { return level.load(std::memory_order_relaxed); }
	bool is_speaking() const { return speaking.load(std::memory_order_relaxed); }

private:
	// Decodes or conceals one event's audio into `pcm`. Returns frames.
	int render(const JitterBuffer::Event &p_event);
	int conceal(const JitterBuffer::Event &p_event);
	void append_to_ring(const float *p_pcm, int p_frames);
	void append_silence(int p_frames);
	void reset_locked();

	// Guards the jitter buffer and settings; held only briefly so pushing a
	// packet never waits for a decode.
	mutable std::mutex state_mutex;
	JitterBuffer jitter;
	Settings settings;

	// Guards everything decode-side: the decoder, ring and scratch buffers.
	// Only readers take it, so it's uncontended on a single audio thread.
	mutable std::mutex mix_mutex;
	Decoder decoder;
	int decoder_channels = 0;
	JitterBuffer::Event event;
	std::vector<float> pcm;
	std::vector<float> ring;	  // RING_FRAMES * decoder_channels
	int64_t written = 0;		  // frames written to the ring, ever
	int64_t last_packet_end = -1; // `written` after the last real packet
	uint16_t dred_seq = 0;		  // packet whose DRED data is parsed
	bool dred_parsed = false;
	int dred_available = 0;
	dsp::FadeIn fade;

	std::atomic<uint64_t> frames_dred{ 0 };
	std::atomic<uint64_t> frames_fec{ 0 };
	std::atomic<uint64_t> frames_plc{ 0 };
	std::atomic<uint64_t> decode_errors{ 0 };
	std::atomic<float> level{ 0.0f };
	std::atomic<bool> speaking{ false };
};

} // namespace godotopus
