#include "godot_opus_resampler.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void GodotOpusResampler::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize", "from_rate", "to_rate", "channels", "quality"),
        &GodotOpusResampler::initialize, DEFVAL(1), DEFVAL(5));
    ClassDB::bind_method(D_METHOD("resample", "input"), &GodotOpusResampler::resample);
    ClassDB::bind_method(D_METHOD("destroy"), &GodotOpusResampler::destroy);
    ClassDB::bind_method(D_METHOD("get_from_rate"), &GodotOpusResampler::get_from_rate);
    ClassDB::bind_method(D_METHOD("get_to_rate"), &GodotOpusResampler::get_to_rate);
}

void GodotOpusResampler::initialize(int p_from_rate, int p_to_rate, int p_channels, int p_quality) {
    destroy();

    from_rate = p_from_rate;
    to_rate = p_to_rate;
    channels = p_channels;

    int err;
    resampler = speex_resampler_init(channels, from_rate, to_rate, p_quality, &err);
    if (err != RESAMPLER_ERR_SUCCESS) {
        UtilityFunctions::printerr("GodotOpusResampler: failed to initialize: ",
            speex_resampler_strerror(err));
        resampler = nullptr;
    }
}

PackedFloat32Array GodotOpusResampler::resample(const PackedFloat32Array &p_input) {
    PackedFloat32Array result;
    if (!resampler) {
        UtilityFunctions::printerr("GodotOpusResampler: not initialized");
        return result;
    }

    spx_uint32_t in_len = p_input.size() / channels;
    spx_uint32_t out_len = (spx_uint32_t)(in_len * (to_rate / (float)from_rate)) + 8;
    result.resize(out_len * channels);

    spx_uint32_t in_consumed = in_len;
    spx_uint32_t out_produced = out_len;

    int err = speex_resampler_process_interleaved_float(
        resampler,
        p_input.ptr(),
        &in_consumed,
        result.ptrw(),
        &out_produced
    );

    if (err != RESAMPLER_ERR_SUCCESS) {
        UtilityFunctions::printerr("GodotOpusResampler: resample failed: ",
            speex_resampler_strerror(err));
        result.clear();
        return result;
    }

    result.resize(out_produced * channels);
    return result;
}

void GodotOpusResampler::destroy() {
    if (resampler) {
        speex_resampler_destroy(resampler);
        resampler = nullptr;
    }
}

GodotOpusResampler::~GodotOpusResampler() {
    destroy();
}
