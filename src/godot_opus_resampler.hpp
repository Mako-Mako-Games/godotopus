#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <speex/speex_resampler.h>

namespace godot {

class GodotOpusResampler : public RefCounted {
    GDCLASS(GodotOpusResampler, RefCounted)

    SpeexResamplerState *resampler = nullptr;
    int channels = 1;
    int from_rate = 0;
    int to_rate = 0;

protected:
    static void _bind_methods();

public:
    // quality 0-10; 5 is a good default for voice
    void initialize(int p_from_rate, int p_to_rate, int p_channels = 1, int p_quality = 5);
    PackedFloat32Array resample(const PackedFloat32Array &p_input);
    void destroy();

    int get_from_rate() const { return from_rate; }
    int get_to_rate() const { return to_rate; }

    ~GodotOpusResampler();
};

} // namespace godot
