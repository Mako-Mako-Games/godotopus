#pragma once

#include "core/capture_pipeline.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <vector>

namespace godot {

// Turns captured audio into Opus packets ready to send: downmixes,
// resamples from the device rate, frames and encodes it, and holds back
// frames Opus classifies as silence. Drive it from the main thread; the
// VoiceSender node does this for you.
class VoiceCapturePipeline : public RefCounted {
	GDCLASS(VoiceCapturePipeline, RefCounted)

	godotopus::CapturePipeline pipeline;
	godotopus::CapturePipeline::Settings settings;
	std::vector<godotopus::CapturePipeline::Packet> packets;
	std::vector<float> scratch; // only when Vector2 isn't single precision

	void apply_settings();

protected:
	static void _bind_methods();

public:
	VoiceCapturePipeline();

	// Takes stereo frames at input_mix_rate and returns the packets to send,
	// oldest first. Each one should get the next sequence number.
	TypedArray<PackedByteArray> process(const PackedVector2Array &p_frames);
	void reset();

	bool is_speaking() const;
	float get_level() const;
	Dictionary get_stats() const;
	bool has_dred_support() const;

	void set_input_mix_rate(int p_rate);
	int get_input_mix_rate() const;
	void set_sample_rate(int p_rate);
	int get_sample_rate() const;
	void set_channels(int p_channels);
	int get_channels() const;
	void set_frame_duration_ms(int p_ms);
	int get_frame_duration_ms() const;
	void set_bitrate(int p_bitrate);
	int get_bitrate() const;
	void set_complexity(int p_complexity);
	int get_complexity() const;
	void set_inband_fec(bool p_enabled);
	bool get_inband_fec() const;
	void set_expected_packet_loss(int p_percent);
	int get_expected_packet_loss() const;
	void set_dtx(bool p_enabled);
	bool get_dtx() const;
	void set_dred_duration_ms(int p_ms);
	int get_dred_duration_ms() const;
	void set_max_backlog_ms(int p_ms);
	int get_max_backlog_ms() const;
	void set_speaking_timeout_ms(int p_ms);
	int get_speaking_timeout_ms() const;
};

} // namespace godot
