#include "godotopus_resampler.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

void GodotOpusResampler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "from_rate", "to_rate", "channels", "quality"), &GodotOpusResampler::initialize, DEFVAL(1), DEFVAL(5));
	ClassDB::bind_method(D_METHOD("resample", "input"), &GodotOpusResampler::resample);
	ClassDB::bind_method(D_METHOD("destroy"), &GodotOpusResampler::destroy);
	ClassDB::bind_method(D_METHOD("get_from_rate"), &GodotOpusResampler::get_from_rate);
	ClassDB::bind_method(D_METHOD("get_to_rate"), &GodotOpusResampler::get_to_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpusResampler::get_channels);
}

void GodotOpusResampler::initialize(int p_from_rate, int p_to_rate, int p_channels, int p_quality) {
	const int error = resampler.init(p_from_rate, p_to_rate, p_channels, p_quality);
	if (error != RESAMPLER_ERR_SUCCESS) {
		UtilityFunctions::push_error("GodotOpusResampler: failed to initialize: ", godotopus::Resampler::error_string(error));
	}
}

PackedFloat32Array GodotOpusResampler::resample(const PackedFloat32Array &p_input) {
	PackedFloat32Array result;
	if (!resampler.is_initialized()) {
		UtilityFunctions::push_error("GodotOpusResampler: not initialized.");
		return result;
	}
	if (p_input.size() % resampler.get_channels() != 0) {
		UtilityFunctions::push_error("GodotOpusResampler: input size ", p_input.size(), " is not a multiple of ", resampler.get_channels(), " channels.");
		return result;
	}

	const int error = resampler.process(p_input.ptr(), p_input.size() / resampler.get_channels(), output);
	if (error != RESAMPLER_ERR_SUCCESS) {
		UtilityFunctions::push_error("GodotOpusResampler: resample failed: ", godotopus::Resampler::error_string(error));
		return result;
	}
	result.resize(static_cast<int64_t>(output.size()));
	if (!output.empty()) {
		std::memcpy(result.ptrw(), output.data(), output.size() * sizeof(float));
	}
	return result;
}

void GodotOpusResampler::destroy() {
	resampler.release();
}

int GodotOpusResampler::get_from_rate() const {
	return resampler.get_from_rate();
}

int GodotOpusResampler::get_to_rate() const {
	return resampler.get_to_rate();
}

int GodotOpusResampler::get_channels() const {
	return resampler.get_channels();
}
