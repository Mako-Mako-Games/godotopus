#pragma once

#include "core/decoder.hpp"
#include "core/encoder.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <vector>

namespace godot {

// Low-level Opus encoder. Most projects should use the voice nodes instead.
class GodotOpusEncoder : public RefCounted {
	GDCLASS(GodotOpusEncoder, RefCounted)

	godotopus::Encoder encoder;
	std::vector<uint8_t> packet;
	int frame_size = 960; // samples per channel; 20 ms at 48 kHz

protected:
	static void _bind_methods();

public:
	// p_sample_rate: 8000, 12000, 16000, 24000 or 48000. p_channels: 1 or 2.
	// p_frame_duration_ms: 5, 10, 20, 40 or 60.
	void initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms = 20);

	// Encodes exactly get_frame_size() * get_channels() samples. A result of
	// 2 bytes or less is a DTX frame and doesn't need to be sent.
	PackedByteArray encode(const PackedFloat32Array &p_pcm);

	void set_bitrate(int p_bitrate);
	int get_bitrate() const;
	void set_complexity(int p_complexity);
	void set_vbr(bool p_enabled, bool p_constrained = false);
	void set_signal_voice(bool p_enabled);
	void set_inband_fec(bool p_enabled);
	void set_expected_packet_loss(int p_percent);
	void set_dtx(bool p_enabled);
	bool is_in_dtx() const;

	// No-op on builds without DNN support; see has_dred_support().
	void set_dred_duration_ms(int p_duration_ms);
	int get_dred_duration_ms() const;
	bool has_dred_support() const;

	void reset();

	// Samples per channel per encode() call. Must be a frame size Opus
	// accepts at the configured sample rate.
	void set_frame_size(int p_frame_size);
	int get_frame_size() const;
	int get_sample_rate() const;
	int get_channels() const;
};

// Low-level Opus decoder. decode() sizes its output from each packet, so it
// handles any frame duration the sender uses. Concealment methods (PLC, FEC,
// DRED) produce get_frame_size() samples per channel.
class GodotOpusDecoder : public RefCounted {
	GDCLASS(GodotOpusDecoder, RefCounted)

	godotopus::Decoder decoder;
	std::vector<float> pcm;
	int frame_size = 960;

	PackedFloat32Array to_packed(int p_result, const char *p_what);

protected:
	static void _bind_methods();

public:
	// p_frame_duration_ms sets the concealment length (see set_frame_size).
	void initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms = 20);

	// Decodes a received packet. An empty packet is treated as lost (PLC).
	PackedFloat32Array decode(const PackedByteArray &p_packet);

	// Conceals one missing frame.
	PackedFloat32Array decode_plc();

	// Recovers the frame lost just before p_next_packet from its in-band FEC
	// data, falling back to concealment. Call decode(p_next_packet) afterwards
	// for that packet's own audio.
	PackedFloat32Array decode_fec(const PackedByteArray &p_next_packet);

	// Deep REDundancy recovery for multi-frame gaps. parse_dred() returns how
	// many samples before p_packet can be recovered (0 if none, or on builds
	// without DNN support). Then, for each missing frame k frames back:
	//     if k * get_frame_size() <= available: decode_dred(k * get_frame_size())
	//     else: decode_plc()
	// and finally decode(p_packet) for the packet's own audio.
	int parse_dred(const PackedByteArray &p_packet);
	PackedFloat32Array decode_dred(int p_samples_back);

	void reset();

	// 0..10. Higher values enable deep PLC and OSCE enhancement on DNN builds.
	void set_complexity(int p_complexity);
	int get_complexity() const;

	// OSCE blind bandwidth extension. No-op on builds without it.
	void set_bandwidth_extension(bool p_enabled);
	bool get_bandwidth_extension() const;
	bool has_bandwidth_extension_support() const;

	void set_frame_size(int p_frame_size);
	int get_frame_size() const;
	int get_sample_rate() const;
	int get_channels() const;
};

} // namespace godot
