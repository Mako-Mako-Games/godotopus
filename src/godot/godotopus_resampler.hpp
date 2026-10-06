#pragma once

#include "core/resampler.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <vector>

namespace godot {

// Streaming sample-rate converter for interleaved float PCM.
class GodotOpusResampler : public RefCounted {
	GDCLASS(GodotOpusResampler, RefCounted)

	godotopus::Resampler resampler;
	std::vector<float> output;

protected:
	static void _bind_methods();

public:
	void initialize(int p_from_rate, int p_to_rate, int p_channels = 1, int p_quality = 5);
	// Converts all of p_input. Output length follows the rate ratio over time.
	PackedFloat32Array resample(const PackedFloat32Array &p_input);
	void destroy();

	int get_from_rate() const;
	int get_to_rate() const;
	int get_channels() const;
};

} // namespace godot
