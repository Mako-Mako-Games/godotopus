#pragma once

// NOTE: only godotopus_codec.cpp was provided to me, not the matching .hpp,
// so this header is reconstructed from how the .cpp used it. The public API
// surface from your original file (initialize, encode, decode, set_bitrate,
// set_frame_size, get_frame_size) is kept intact and backward compatible —
// initialize() just gained an optional third argument — so existing call
// sites won't need changes. Please diff this against your real header before
// dropping it in, in case the base class or includes differ in your project.

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <opus.h>

namespace godot {

class GodotOpusEncoder : public RefCounted {
	GDCLASS(GodotOpusEncoder, RefCounted)

private:
	OpusEncoder *encoder = nullptr;
	int sample_rate = 48000;
	int channels = 1;
	int frame_size = 960; // 20ms @ 48kHz

protected:
	static void _bind_methods();

public:
	// frame_duration_ms must be one of 5, 10, 20, 40, 60 — the durations Opus
	// natively supports. 20ms is the standard VoIP default (good latency vs.
	// overhead tradeoff); falls back to 20ms with an error if given anything else.
	void initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms = 20);

	PackedByteArray encode(const PackedFloat32Array &p_pcm);

	void set_bitrate(int p_bitrate);
	int get_bitrate() const;

	void set_complexity(int p_complexity); // 0 (fastest/cheapest) .. 10 (best quality)
	void set_vbr(bool p_enabled, bool p_constrained = false);
	void set_signal_voice(bool p_enabled); // hint the encoder this is speech, not music
	void set_inband_fec(bool p_enabled);
	void set_expected_packet_loss(int p_percent); // 0-100, tunes how much FEC redundancy is spent
	void set_dtx(bool p_enabled);

	void reset(); // cheap OPUS_RESET_STATE — use between speech bursts instead of recreating

	// Manual override of frame size in samples-per-channel. Opus itself allows
	// the frame size to vary call-to-call as long as each value corresponds to
	// a supported duration at the configured sample rate, so this is safe to
	// call without reinitializing — it only affects the next encode() call.
	void set_frame_size(int p_frame_size);
	int get_frame_size() const;
	int get_sample_rate() const;
	int get_channels() const;

	GodotOpusEncoder() {}
	~GodotOpusEncoder();
};

class GodotOpusDecoder : public RefCounted {
	GDCLASS(GodotOpusDecoder, RefCounted)

private:
	OpusDecoder *decoder = nullptr;
	int sample_rate = 48000;
	int channels = 1;
	int frame_size = 960;

protected:
	static void _bind_methods();

public:
	void initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms = 20);

	// Normal decode of a packet that actually arrived.
	PackedFloat32Array decode(const PackedByteArray &p_packet);

	// Pure packet-loss concealment: call when a frame is missing and you have
	// no later packet yet to attempt FEC recovery from (i.e. you can't wait
	// any longer without adding latency).
	PackedFloat32Array decode_plc();

	// Recovers the frame that was lost immediately BEFORE p_next_packet, using
	// p_next_packet's embedded in-band FEC data (if the sender had FEC enabled
	// and judged it worth including). Internally falls back to concealment if
	// no usable FEC data is present, so it's always safe to call on a gap.
	//
	// IMPORTANT: this does not decode p_next_packet's own audio. You must
	// separately call decode(p_next_packet) afterwards to get that frame —
	// that's the real per-packet Opus FEC contract, not a single combined call.
	PackedFloat32Array decode_fec(const PackedByteArray &p_next_packet);

	void reset(); // cheap OPUS_RESET_STATE — use at stream/speech-burst boundaries

	void set_frame_size(int p_frame_size);
	int get_frame_size() const;
	int get_sample_rate() const;
	int get_channels() const;

	GodotOpusDecoder() {}
	~GodotOpusDecoder();
};

} // namespace godot
