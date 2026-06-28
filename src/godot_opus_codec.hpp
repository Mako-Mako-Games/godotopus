#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include "opus.h"

// Alias raw opus types to avoid collision with our class names
using OpusEncoderState = OpusEncoder;
using OpusDecoderState = OpusDecoder;

namespace godot {

    class GodotOpusEncoder : public RefCounted {
        GDCLASS(GodotOpusEncoder, RefCounted)

    private:
        OpusEncoderState *encoder = nullptr;  // ← raw opus type, unambiguous
        int sample_rate = 48000;
        int channels = 1;
        int frame_size = 960;

    public:
        static void _bind_methods();
        void initialize(int p_sample_rate, int p_channels);
        PackedByteArray encode(const PackedFloat32Array &p_pcm);
        void set_bitrate(int p_bitrate);
        void set_frame_size(int p_frame_size);
        int get_frame_size() const;
        ~GodotOpusEncoder();
    };

    class GodotOpusDecoder : public RefCounted {
        GDCLASS(GodotOpusDecoder, RefCounted)

    private:
        OpusDecoderState *decoder = nullptr;  // ← raw opus type, unambiguous
        int sample_rate = 48000;
        int channels = 1;
        int frame_size = 960;

    public:
        static void _bind_methods();
        void initialize(int p_sample_rate, int p_channels);
        PackedFloat32Array decode(const PackedByteArray &p_packet);
        void set_frame_size(int p_frame_size);
        int get_frame_size() const;
        ~GodotOpusDecoder();
    };

} // namespace godot