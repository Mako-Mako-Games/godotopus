#pragma once

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

	// Always call this for every captured frame, DTX-on or off — it's cheap
	// and Opus itself decides whether there's anything worth sending. With
	// DTX enabled (see set_dtx), silence returns a 0-byte array most of the
	// time (nothing to transmit) and only occasionally a tiny comfort-noise
	// packet; that's the entire "is this speech" decision, no app-level VAD
	// needed. Callers should simply skip transmitting when the result is empty.
	PackedByteArray encode(const PackedFloat32Array &p_pcm);

	void set_bitrate(int p_bitrate);
	int get_bitrate() const;

	void set_complexity(int p_complexity); // 0 (fastest/cheapest) .. 10 (best quality)
	void set_vbr(bool p_enabled, bool p_constrained = false);
	void set_signal_voice(bool p_enabled); // hint the encoder this is speech, not music
	void set_inband_fec(bool p_enabled);
	void set_expected_packet_loss(int p_percent); // 0-100, tunes how much FEC redundancy is spent

	// Opus's own RNN-based voice activity detector + discontinuous
	// transmission. When enabled, Opus stops encoding meaningful data during
	// silence (encode() starts returning mostly-empty packets) instead of an
	// app-level amplitude gate having to guess. This is what libopus 1.3+
	// ships internally — there is no reason to reimplement it.
	void set_dtx(bool p_enabled);
	// True if the most recently encoded frame was classified as silence/DTX
	// comfort-noise by Opus's own detector (OPUS_GET_IN_DTX). Purely
	// informational (e.g. for a "is this player speaking" UI indicator) —
	// the encode() return size is already sufficient for transmit gating.
	bool is_in_dtx() const;

	// Deep REDundancy (Opus 1.5+): embeds a compressed, DNN-encoded history
	// of recent audio in every packet so a receiver that's missed several
	// frames in a row can recover far more of them than plain in-band FEC
	// (which only ever recovers the single previous frame). Duration is how
	// much history to embed, in milliseconds (rounded down to 10ms steps,
	// clamped to what libopus supports — currently up to ~1000ms). 0 disables it.
	void set_dred_duration_ms(int p_duration_ms);
	int get_dred_duration_ms() const;
	// True if this build of libopus actually has DRED compiled in (with
	// model weights loaded). If false, set_dred_duration_ms() is a harmless
	// no-op — safe to call unconditionally either way.
	bool has_dred_support() const;

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
	OpusDREDDecoder *dred_decoder = nullptr;
	OpusDRED *dred_state = nullptr;
	bool dred_state_valid = false;
	int sample_rate = 48000;
	int channels = 1;
	int frame_size = 960;

	void _ensure_dred_decoder();

protected:
	static void _bind_methods();

public:
	void initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms = 20);

	// Normal decode of a packet that actually arrived.
	PackedFloat32Array decode(const PackedByteArray &p_packet);

	// Pure packet-loss concealment: call when a frame is missing and you have
	// no later packet yet to attempt FEC/DRED recovery from (i.e. you can't
	// wait any longer without adding latency). Automatically benefits from
	// libopus 1.5+'s DNN-based concealment (LPCNet) if this build has it —
	// no extra API surface needed, it's just a better result from the same call.
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

	// ── Deep REDundancy (DRED) recovery ────────────────────────────────
	// DRED reaches much further back than plain FEC (up to ~1s of history,
	// vs. FEC's single previous frame), which matters a lot for a jitter
	// buffer recovering from a multi-frame gap. Usage on a gap of N missing
	// frames before p_packet:
	//   var samples_back = parse_dred(p_packet)  # 0 if unavailable
	//   for k in range(N, 0, -1):
	//       var needed = k * get_frame_size()
	//       if samples_back >= needed:
	//           decode_dred(needed)   # recovered via DRED
	//       else:
	//           decode_plc()          # DRED didn't reach this far back
	//   decode(p_packet)              # the packet's own audio, always last

	// Parses p_packet's embedded DRED payload (if any). Returns the number
	// of samples of history available for decode_dred() (0 if the packet has
	// none — sender didn't have DRED enabled, or this build lacks DRED support).
	int parse_dred(const PackedByteArray &p_packet);
	// Decodes the frame p_samples_back samples before the packet last passed
	// to parse_dred(). Must be called after a parse_dred() that returned a
	// value >= p_samples_back. Returns an empty array if unavailable.
	PackedFloat32Array decode_dred(int p_samples_back);
	// Note: there's no has_dred_support() here — parse_dred() already returns
	// 0 whenever DRED isn't usable (build lacks it, or this packet has none),
	// which is the only place that actually matters at runtime. Use the
	// encoder's has_dred_support() if you just want a capability flag for UI.

	void reset(); // cheap OPUS_RESET_STATE — use at stream/speech-burst boundaries

	// 0 (fastest/cheapest) .. 10 (best quality). Also gates libopus 1.5+'s
	// DNN-based PLC (needs >=5) and OSCE speech enhancement (LACE >=6,
	// NoLACE >=7) — higher complexity here buys a meaningfully better result
	// on packet loss, not just CPU cost, so prefer keeping this high unless
	// you're CPU constrained.
	void set_complexity(int p_complexity);
	int get_complexity() const;

	// OSCE blind Bandwidth Extension (Opus 1.6+): reconstructs a wideband
	// (8kHz passband) SILK signal decoded at 48kHz up to fullband quality,
	// without the sender spending any extra bits. Only kicks in when the
	// stream is actually running narrower than the decoder's own output rate.
	void set_bandwidth_extension(bool p_enabled);
	bool get_bandwidth_extension() const;
	// True if this build of libopus actually has OSCE/BWE compiled in.
	bool has_bandwidth_extension_support() const;

	void set_frame_size(int p_frame_size);
	int get_frame_size() const;
	int get_sample_rate() const;
	int get_channels() const;

	GodotOpusDecoder() {}
	~GodotOpusDecoder();
};

} // namespace godot
