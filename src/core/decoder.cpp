#include "decoder.hpp"

#include "frame.hpp"

namespace godotopus {

int inspect_packet(const uint8_t *p_data, size_t p_size, int p_sample_rate, PacketInfo &r_info) {
	r_info = PacketInfo();
	if (!p_data || p_size == 0) {
		return OPUS_BAD_ARG;
	}
	const opus_int32 size = static_cast<opus_int32>(p_size);
	const int frames = opus_packet_get_nb_frames(p_data, size);
	if (frames < 0) {
		return frames;
	}
	const int samples = opus_packet_get_nb_samples(p_data, size, p_sample_rate);
	if (samples < 0) {
		return samples;
	}
	r_info.samples = samples;
	r_info.channels = opus_packet_get_nb_channels(p_data);
	r_info.frames = frames;
	return OPUS_OK;
}

Decoder::~Decoder() {
	destroy();
}

void Decoder::destroy() {
	if (decoder) {
		opus_decoder_destroy(decoder);
		decoder = nullptr;
	}
	if (dred_decoder) {
		opus_dred_decoder_destroy(dred_decoder);
		dred_decoder = nullptr;
	}
	if (dred) {
		opus_dred_free(dred);
		dred = nullptr;
	}
	dred_unavailable = false;
	dred_samples = 0;
	last_packet_samples = 0;
	sample_rate = 0;
	channels = 0;
}

int Decoder::init(int p_sample_rate, int p_channels) {
	destroy();
	if (!is_valid_sample_rate(p_sample_rate) || (p_channels != 1 && p_channels != 2)) {
		return OPUS_BAD_ARG;
	}

	int error = OPUS_OK;
	decoder = opus_decoder_create(p_sample_rate, p_channels, &error);
	if (error != OPUS_OK) {
		decoder = nullptr;
		return error;
	}
	sample_rate = p_sample_rate;
	channels = p_channels;
	return OPUS_OK;
}

int Decoder::finish(int p_result, std::vector<float> &r_pcm) {
	r_pcm.resize(p_result > 0 ? static_cast<size_t>(p_result) * channels : 0);
	return p_result;
}

int Decoder::decode(const uint8_t *p_data, size_t p_size, std::vector<float> &r_pcm) {
	r_pcm.clear();
	if (!decoder) {
		return OPUS_INVALID_STATE;
	}
	if (!p_data || p_size == 0) {
		return OPUS_BAD_ARG;
	}

	const int max_samples = frame_samples(sample_rate, MAX_PACKET_DURATION_MS);
	r_pcm.resize(static_cast<size_t>(max_samples) * channels);
	const int result = opus_decode_float(decoder, p_data, static_cast<opus_int32>(p_size), r_pcm.data(), max_samples, 0);
	if (result > 0) {
		last_packet_samples = result;
	}
	return finish(result, r_pcm);
}

int Decoder::conceal(int p_frame_samples, std::vector<float> &r_pcm) {
	r_pcm.clear();
	if (!decoder) {
		return OPUS_INVALID_STATE;
	}
	if (!is_valid_frame_samples(sample_rate, p_frame_samples)) {
		return OPUS_BAD_ARG;
	}

	r_pcm.resize(static_cast<size_t>(p_frame_samples) * channels);
	return finish(opus_decode_float(decoder, nullptr, 0, r_pcm.data(), p_frame_samples, 0), r_pcm);
}

int Decoder::decode_fec(const uint8_t *p_data, size_t p_size, int p_frame_samples, std::vector<float> &r_pcm) {
	if (!p_data || p_size == 0) {
		return conceal(p_frame_samples, r_pcm);
	}
	r_pcm.clear();
	if (!decoder) {
		return OPUS_INVALID_STATE;
	}
	if (!is_valid_frame_samples(sample_rate, p_frame_samples)) {
		return OPUS_BAD_ARG;
	}

	r_pcm.resize(static_cast<size_t>(p_frame_samples) * channels);
	const int result = opus_decode_float(decoder, p_data, static_cast<opus_int32>(p_size), r_pcm.data(), p_frame_samples, 1);
	return finish(result, r_pcm);
}

bool Decoder::ensure_dred() {
	if (dred_decoder && dred) {
		return true;
	}
	if (dred_unavailable) {
		return false;
	}

	int error = OPUS_OK;
	dred_decoder = opus_dred_decoder_create(&error);
	if (error == OPUS_OK && dred_decoder) {
		dred = opus_dred_alloc(&error);
	}
	if (error != OPUS_OK || !dred_decoder || !dred) {
		// Builds without ENABLE_DRED report OPUS_UNIMPLEMENTED; don't retry.
		if (dred_decoder) {
			opus_dred_decoder_destroy(dred_decoder);
			dred_decoder = nullptr;
		}
		if (dred) {
			opus_dred_free(dred);
			dred = nullptr;
		}
		dred_unavailable = true;
		return false;
	}
	return true;
}

int Decoder::parse_dred(const uint8_t *p_data, size_t p_size) {
	dred_samples = 0;
	if (!decoder || !p_data || p_size == 0 || !ensure_dred()) {
		return 0;
	}

	int dred_end = 0;
	// Look back at most one second.
	const int result = opus_dred_parse(dred_decoder, dred, p_data, static_cast<opus_int32>(p_size), sample_rate, sample_rate, &dred_end, 0);
	dred_samples = result > 0 ? result : 0;
	return dred_samples;
}

int Decoder::decode_dred(int p_offset_samples, int p_frame_samples, std::vector<float> &r_pcm) {
	r_pcm.clear();
	if (!decoder) {
		return OPUS_INVALID_STATE;
	}
	if (p_offset_samples <= 0 || p_offset_samples > dred_samples || !is_valid_frame_samples(sample_rate, p_frame_samples)) {
		return OPUS_BAD_ARG;
	}

	r_pcm.resize(static_cast<size_t>(p_frame_samples) * channels);
	const int result = opus_decoder_dred_decode_float(decoder, dred, p_offset_samples, r_pcm.data(), p_frame_samples);
	return finish(result, r_pcm);
}

int Decoder::set_complexity(int p_complexity) {
	return decoder ? opus_decoder_ctl(decoder, OPUS_SET_COMPLEXITY(p_complexity)) : OPUS_INVALID_STATE;
}

int Decoder::get_complexity() const {
	opus_int32 complexity = 0;
	if (decoder) {
		opus_decoder_ctl(decoder, OPUS_GET_COMPLEXITY(&complexity));
	}
	return complexity;
}

int Decoder::set_bandwidth_extension(bool p_enabled) {
	return decoder ? opus_decoder_ctl(decoder, OPUS_SET_OSCE_BWE(p_enabled ? 1 : 0)) : OPUS_INVALID_STATE;
}

bool Decoder::get_bandwidth_extension() const {
	opus_int32 enabled = 0;
	if (decoder) {
		opus_decoder_ctl(decoder, OPUS_GET_OSCE_BWE(&enabled));
	}
	return enabled != 0;
}

bool Decoder::supports_bandwidth_extension() const {
	opus_int32 enabled = 0;
	return decoder && opus_decoder_ctl(decoder, OPUS_GET_OSCE_BWE(&enabled)) == OPUS_OK;
}

int Decoder::reset() {
	dred_samples = 0;
	last_packet_samples = 0;
	return decoder ? opus_decoder_ctl(decoder, OPUS_RESET_STATE) : OPUS_INVALID_STATE;
}

} // namespace godotopus
