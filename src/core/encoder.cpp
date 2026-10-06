#include "encoder.hpp"

#include "frame.hpp"

namespace godotopus {

namespace {
// DRED duration is configured in 10 ms units.
constexpr int DRED_UNIT_MS = 10;
} // namespace

Encoder::~Encoder() {
	destroy();
}

void Encoder::destroy() {
	if (encoder) {
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
	sample_rate = 0;
	channels = 0;
}

int Encoder::init(int p_sample_rate, int p_channels) {
	destroy();
	if (!is_valid_sample_rate(p_sample_rate) || (p_channels != 1 && p_channels != 2)) {
		return OPUS_BAD_ARG;
	}

	int error = OPUS_OK;
	encoder = opus_encoder_create(p_sample_rate, p_channels, OPUS_APPLICATION_VOIP, &error);
	if (error != OPUS_OK) {
		encoder = nullptr;
		return error;
	}
	sample_rate = p_sample_rate;
	channels = p_channels;

	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
	opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
	opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
	return OPUS_OK;
}

int Encoder::encode(const float *p_pcm, int p_frame_samples, std::vector<uint8_t> &r_packet) {
	r_packet.clear();
	if (!encoder) {
		return OPUS_INVALID_STATE;
	}
	if (!p_pcm || !is_valid_frame_samples(sample_rate, p_frame_samples)) {
		return OPUS_BAD_ARG;
	}

	r_packet.resize(MAX_PACKET_BYTES);
	const int bytes = opus_encode_float(encoder, p_pcm, p_frame_samples, r_packet.data(), MAX_PACKET_BYTES);
	r_packet.resize(bytes > 0 ? bytes : 0);
	return bytes;
}

int Encoder::set_bitrate(int p_bits_per_second) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_BITRATE(p_bits_per_second)) : OPUS_INVALID_STATE;
}

int Encoder::get_bitrate() const {
	opus_int32 bitrate = 0;
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_GET_BITRATE(&bitrate));
	}
	return bitrate;
}

int Encoder::set_complexity(int p_complexity) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(p_complexity)) : OPUS_INVALID_STATE;
}

int Encoder::set_vbr(bool p_enabled, bool p_constrained) {
	if (!encoder) {
		return OPUS_INVALID_STATE;
	}
	const int error = opus_encoder_ctl(encoder, OPUS_SET_VBR(p_enabled ? 1 : 0));
	if (error != OPUS_OK) {
		return error;
	}
	return opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(p_constrained ? 1 : 0));
}

int Encoder::set_signal_voice(bool p_enabled) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(p_enabled ? OPUS_SIGNAL_VOICE : OPUS_AUTO)) : OPUS_INVALID_STATE;
}

int Encoder::set_inband_fec(bool p_enabled) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(p_enabled ? 1 : 0)) : OPUS_INVALID_STATE;
}

int Encoder::set_expected_packet_loss(int p_percent) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(p_percent)) : OPUS_INVALID_STATE;
}

int Encoder::set_dtx(bool p_enabled) {
	return encoder ? opus_encoder_ctl(encoder, OPUS_SET_DTX(p_enabled ? 1 : 0)) : OPUS_INVALID_STATE;
}

bool Encoder::is_in_dtx() const {
	opus_int32 in_dtx = 0;
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_GET_IN_DTX(&in_dtx));
	}
	return in_dtx != 0;
}

int Encoder::set_dred_duration_ms(int p_duration_ms) {
	if (!encoder) {
		return OPUS_INVALID_STATE;
	}
	const int units = p_duration_ms > 0 ? p_duration_ms / DRED_UNIT_MS : 0;
	return opus_encoder_ctl(encoder, OPUS_SET_DRED_DURATION(units));
}

int Encoder::get_dred_duration_ms() const {
	opus_int32 units = 0;
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_GET_DRED_DURATION(&units));
	}
	return units * DRED_UNIT_MS;
}

bool Encoder::supports_dred() const {
	// The request only exists in builds compiled with ENABLE_DRED.
	opus_int32 units = 0;
	return encoder && opus_encoder_ctl(encoder, OPUS_GET_DRED_DURATION(&units)) == OPUS_OK;
}

int Encoder::reset() {
	return encoder ? opus_encoder_ctl(encoder, OPUS_RESET_STATE) : OPUS_INVALID_STATE;
}

} // namespace godotopus
