#pragma once

#include "core/voice_source.hpp"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback_resampled.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace godot {

// One remote voice as an AudioStream. Feed it packets with push_packet()
// and assign it to any number of AudioStreamPlayer, AudioStreamPlayer2D or
// AudioStreamPlayer3D nodes; each one plays the same voice. Decoding
// happens on the audio thread as the players need audio, at 48 kHz, and
// the engine resamples to the output rate.
class VoiceStream : public AudioStream {
	GDCLASS(VoiceStream, AudioStream)

	godotopus::VoiceSource source;
	godotopus::VoiceSource::Settings settings;

	void apply_settings();

protected:
	static void _bind_methods();

public:
	VoiceStream();

	// Thread-safe. p_sequence counts transmitted packets and wraps at 65536.
	void push_packet(const PackedByteArray &p_packet, int p_sequence);
	void reset();

	Dictionary get_stats() const;
	float get_level() const;
	bool is_speaking() const;

	void set_target_latency_ms(int p_ms);
	int get_target_latency_ms() const;
	void set_max_latency_ms(int p_ms);
	int get_max_latency_ms() const;
	void set_max_conceal_ms(int p_ms);
	int get_max_conceal_ms() const;
	void set_channels(int p_channels);
	int get_channels() const;
	void set_decoder_complexity(int p_complexity);
	int get_decoder_complexity() const;
	void set_bandwidth_extension(bool p_enabled);
	bool get_bandwidth_extension() const;
	void set_speaking_timeout_ms(int p_ms);
	int get_speaking_timeout_ms() const;

	godotopus::VoiceSource &get_source() { return source; }

	Ref<AudioStreamPlayback> _instantiate_playback() const override;
	String _get_stream_name() const override;
	double _get_length() const override;
	bool _is_monophonic() const override;
};

// One player's view of a VoiceStream. Created by the engine when a player
// starts; joins the voice live.
class VoiceStreamPlayback : public AudioStreamPlaybackResampled {
	GDCLASS(VoiceStreamPlayback, AudioStreamPlaybackResampled)

	friend class VoiceStream;

	Ref<VoiceStream> stream;
	godotopus::VoiceSource::Cursor cursor = 0;
	bool active = false;

protected:
	static void _bind_methods() {}

public:
	void _start(double p_from_pos) override;
	void _stop() override;
	bool _is_playing() const override;
	int32_t _get_loop_count() const override;
	double _get_playback_position() const override;
	void _seek(double p_position) override;

	int32_t _mix_resampled(AudioFrame *p_dst_buffer, int32_t p_frame_count) override;
	float _get_stream_sampling_rate() const override;
};

} // namespace godot
