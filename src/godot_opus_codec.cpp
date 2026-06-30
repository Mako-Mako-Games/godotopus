#include "godot_opus_codec.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

// Opus only supports a fixed set of frame durations per sample rate. Anything
// else gets rejected by opus_encode_float/opus_decode_float at runtime with a
// much less helpful error, so we validate up front and fall back to 20ms.
int compute_frame_size(int p_sample_rate, int p_frame_duration_ms) {
	switch (p_frame_duration_ms) {
		case 5:
		case 10:
		case 20:
		case 40:
		case 60:
			return p_sample_rate * p_frame_duration_ms / 1000;
		default:
			UtilityFunctions::printerr(
				"GodotOpusCodec: frame_duration_ms must be one of 5, 10, 20, 40, 60 (got ",
				p_frame_duration_ms, "), falling back to 20ms.");
			return p_sample_rate * 20 / 1000;
	}
}

} // namespace

// ── GodotOpusEncoder ──────────────────────────────────────────────────────────────

void GodotOpusEncoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels", "frame_duration_ms"),
		&GodotOpusEncoder::initialize, DEFVAL(20));
	ClassDB::bind_method(D_METHOD("encode", "pcm"), &GodotOpusEncoder::encode);

	ClassDB::bind_method(D_METHOD("set_bitrate", "bitrate"), &GodotOpusEncoder::set_bitrate);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &GodotOpusEncoder::get_bitrate);
	ClassDB::bind_method(D_METHOD("set_complexity", "complexity"), &GodotOpusEncoder::set_complexity);
	ClassDB::bind_method(D_METHOD("set_vbr", "enabled", "constrained"), &GodotOpusEncoder::set_vbr, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_signal_voice", "enabled"), &GodotOpusEncoder::set_signal_voice);
	ClassDB::bind_method(D_METHOD("set_inband_fec", "enabled"), &GodotOpusEncoder::set_inband_fec);
	ClassDB::bind_method(D_METHOD("set_expected_packet_loss", "percent"), &GodotOpusEncoder::set_expected_packet_loss);
	ClassDB::bind_method(D_METHOD("set_dtx", "enabled"), &GodotOpusEncoder::set_dtx);
	ClassDB::bind_method(D_METHOD("reset"), &GodotOpusEncoder::reset);

	ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusEncoder::set_frame_size);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusEncoder::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &GodotOpusEncoder::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpusEncoder::get_channels);
}

void GodotOpusEncoder::initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms) {
	if (encoder) {
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
	sample_rate = p_sample_rate;
	channels = p_channels;
	frame_size = compute_frame_size(sample_rate, p_frame_duration_ms);

	int error;
	encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
	if (error != OPUS_OK) {
		UtilityFunctions::printerr("GodotOpusEncoder: failed to create encoder: ", opus_strerror(error));
		encoder = nullptr;
		return;
	}

	// Defaults tuned for real-time voice. VoiceTransmitterComponent overrides
	// bitrate/complexity/expected-loss from VoiceConfig right after this call;
	// these are just sane fallbacks if used standalone.
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
	opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
	opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	// DTX deliberately OFF: the app-level VAD already decides when to send at
	// all, so an independent encoder-side silence detector would just be a
	// second, possibly-disagreeing source of truth for the same decision.
	opus_encoder_ctl(encoder, OPUS_SET_DTX(0));
}

PackedByteArray GodotOpusEncoder::encode(const PackedFloat32Array &p_pcm) {
	PackedByteArray result;
	if (!encoder) {
		UtilityFunctions::printerr("GodotOpusEncoder: not initialized");
		return result;
	}
	if (p_pcm.size() != frame_size * channels) {
		UtilityFunctions::printerr(
			"GodotOpusEncoder: expected ", frame_size * channels,
			" samples, got ", p_pcm.size());
		return result;
	}

	const int max_packet = 4000;
	result.resize(max_packet);

	int bytes_written = opus_encode_float(
		encoder,
		p_pcm.ptr(),
		frame_size,
		result.ptrw(),
		max_packet);

	if (bytes_written < 0) {
		UtilityFunctions::printerr("GodotOpusEncoder: encode failed: ", opus_strerror(bytes_written));
		result.clear();
		return result;
	}

	result.resize(bytes_written);
	return result;
}

void GodotOpusEncoder::set_bitrate(int p_bitrate) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(p_bitrate));
	}
}

int GodotOpusEncoder::get_bitrate() const {
	if (!encoder) {
		return 0;
	}
	opus_int32 bitrate = 0;
	opus_encoder_ctl(encoder, OPUS_GET_BITRATE(&bitrate));
	return (int)bitrate;
}

void GodotOpusEncoder::set_complexity(int p_complexity) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(p_complexity));
	}
}

void GodotOpusEncoder::set_vbr(bool p_enabled, bool p_constrained) {
	if (!encoder) {
		return;
	}
	opus_encoder_ctl(encoder, OPUS_SET_VBR(p_enabled ? 1 : 0));
	opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(p_constrained ? 1 : 0));
}

void GodotOpusEncoder::set_signal_voice(bool p_enabled) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(p_enabled ? OPUS_SIGNAL_VOICE : OPUS_AUTO));
	}
}

void GodotOpusEncoder::set_inband_fec(bool p_enabled) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(p_enabled ? 1 : 0));
	}
}

void GodotOpusEncoder::set_expected_packet_loss(int p_percent) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(p_percent));
	}
}

void GodotOpusEncoder::set_dtx(bool p_enabled) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_DTX(p_enabled ? 1 : 0));
	}
}

void GodotOpusEncoder::reset() {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_RESET_STATE);
	}
}

void GodotOpusEncoder::set_frame_size(int p_frame_size) {
	frame_size = p_frame_size;
}

int GodotOpusEncoder::get_frame_size() const {
	return frame_size;
}

int GodotOpusEncoder::get_sample_rate() const {
	return sample_rate;
}

int GodotOpusEncoder::get_channels() const {
	return channels;
}

GodotOpusEncoder::~GodotOpusEncoder() {
	if (encoder) {
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
}

// ── GodotOpusDecoder ──────────────────────────────────────────────────────────────

void GodotOpusDecoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels", "frame_duration_ms"),
		&GodotOpusDecoder::initialize, DEFVAL(20));
	ClassDB::bind_method(D_METHOD("decode", "packet"), &GodotOpusDecoder::decode);
	ClassDB::bind_method(D_METHOD("decode_plc"), &GodotOpusDecoder::decode_plc);
	ClassDB::bind_method(D_METHOD("decode_fec", "next_packet"), &GodotOpusDecoder::decode_fec);
	ClassDB::bind_method(D_METHOD("reset"), &GodotOpusDecoder::reset);

	ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusDecoder::set_frame_size);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusDecoder::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &GodotOpusDecoder::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpusDecoder::get_channels);
}

void GodotOpusDecoder::initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms) {
	if (decoder) {
		opus_decoder_destroy(decoder);
		decoder = nullptr;
	}
	sample_rate = p_sample_rate;
	channels = p_channels;
	frame_size = compute_frame_size(sample_rate, p_frame_duration_ms);

	int error;
	decoder = opus_decoder_create(sample_rate, channels, &error);
	if (error != OPUS_OK) {
		UtilityFunctions::printerr("GodotOpusDecoder: failed to create decoder: ", opus_strerror(error));
		decoder = nullptr;
	}
}

PackedFloat32Array GodotOpusDecoder::decode(const PackedByteArray &p_packet) {
	PackedFloat32Array result;
	if (!decoder) {
		UtilityFunctions::printerr("GodotOpusDecoder: not initialized");
		return result;
	}

	result.resize(frame_size * channels);

	// Opus treats data==NULL (not just length 0 with a valid pointer) as the
	// explicit "no packet, please conceal" signal, so route empty arrays
	// through that path rather than calling ptr() on an empty PackedByteArray.
	const unsigned char *data_ptr = p_packet.size() > 0 ? p_packet.ptr() : nullptr;

	int samples_decoded = opus_decode_float(
		decoder,
		data_ptr,
		p_packet.size(),
		result.ptrw(),
		frame_size,
		0 // this is a normal decode of a packet that arrived, not an FEC request
	);

	if (samples_decoded < 0) {
		UtilityFunctions::printerr("GodotOpusDecoder: decode failed: ", opus_strerror(samples_decoded));
		result.clear();
		return result;
	}

	result.resize(samples_decoded * channels);
	return result;
}

PackedFloat32Array GodotOpusDecoder::decode_plc() {
	PackedFloat32Array result;
	if (!decoder) {
		UtilityFunctions::printerr("GodotOpusDecoder: not initialized");
		return result;
	}

	result.resize(frame_size * channels);
	int samples_decoded = opus_decode_float(decoder, nullptr, 0, result.ptrw(), frame_size, 0);

	if (samples_decoded < 0) {
		UtilityFunctions::printerr("GodotOpusDecoder: PLC failed: ", opus_strerror(samples_decoded));
		result.clear();
		return result;
	}

	result.resize(samples_decoded * channels);
	return result;
}

PackedFloat32Array GodotOpusDecoder::decode_fec(const PackedByteArray &p_next_packet) {
	if (!decoder) {
		UtilityFunctions::printerr("GodotOpusDecoder: not initialized");
		return PackedFloat32Array();
	}
	if (p_next_packet.size() == 0) {
		// Nothing to recover a previous frame from — behave like plain PLC.
		return decode_plc();
	}

	PackedFloat32Array result;
	result.resize(frame_size * channels);

	int samples_decoded = opus_decode_float(
		decoder,
		p_next_packet.ptr(),
		p_next_packet.size(),
		result.ptrw(),
		frame_size,
		1 // decode_fec=1: recover the PREVIOUS frame from this packet's redundancy
	);

	if (samples_decoded < 0) {
		UtilityFunctions::printerr("GodotOpusDecoder: FEC decode failed: ", opus_strerror(samples_decoded));
		result.clear();
		return result;
	}

	result.resize(samples_decoded * channels);
	return result;
}

void GodotOpusDecoder::reset() {
	if (decoder) {
		opus_decoder_ctl(decoder, OPUS_RESET_STATE);
	}
}

void GodotOpusDecoder::set_frame_size(int p_frame_size) {
	frame_size = p_frame_size;
}

int GodotOpusDecoder::get_frame_size() const {
	return frame_size;
}

int GodotOpusDecoder::get_sample_rate() const {
	return sample_rate;
}

int GodotOpusDecoder::get_channels() const {
	return channels;
}

GodotOpusDecoder::~GodotOpusDecoder() {
	if (decoder) {
		opus_decoder_destroy(decoder);
		decoder = nullptr;
	}
}
