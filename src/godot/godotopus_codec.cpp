#include "godotopus_codec.hpp"

#include "core/frame.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace {

int checked_frame_duration_ms(int p_duration_ms) {
	if (godotopus::is_valid_frame_duration_ms(p_duration_ms)) {
		return p_duration_ms;
	}
	UtilityFunctions::push_error("Godotopus: frame_duration_ms must be 5, 10, 20, 40 or 60 (got ", p_duration_ms, "); using 20.");
	return 20;
}

} // namespace

// GodotOpusEncoder

void GodotOpusEncoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels", "frame_duration_ms"), &GodotOpusEncoder::initialize, DEFVAL(20));
	ClassDB::bind_method(D_METHOD("encode", "pcm"), &GodotOpusEncoder::encode);

	ClassDB::bind_method(D_METHOD("set_bitrate", "bitrate"), &GodotOpusEncoder::set_bitrate);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &GodotOpusEncoder::get_bitrate);
	ClassDB::bind_method(D_METHOD("set_complexity", "complexity"), &GodotOpusEncoder::set_complexity);
	ClassDB::bind_method(D_METHOD("set_vbr", "enabled", "constrained"), &GodotOpusEncoder::set_vbr, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_signal_voice", "enabled"), &GodotOpusEncoder::set_signal_voice);
	ClassDB::bind_method(D_METHOD("set_inband_fec", "enabled"), &GodotOpusEncoder::set_inband_fec);
	ClassDB::bind_method(D_METHOD("set_expected_packet_loss", "percent"), &GodotOpusEncoder::set_expected_packet_loss);
	ClassDB::bind_method(D_METHOD("set_dtx", "enabled"), &GodotOpusEncoder::set_dtx);
	ClassDB::bind_method(D_METHOD("is_in_dtx"), &GodotOpusEncoder::is_in_dtx);
	ClassDB::bind_method(D_METHOD("set_dred_duration_ms", "duration_ms"), &GodotOpusEncoder::set_dred_duration_ms);
	ClassDB::bind_method(D_METHOD("get_dred_duration_ms"), &GodotOpusEncoder::get_dred_duration_ms);
	ClassDB::bind_method(D_METHOD("has_dred_support"), &GodotOpusEncoder::has_dred_support);
	ClassDB::bind_method(D_METHOD("reset"), &GodotOpusEncoder::reset);

	ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusEncoder::set_frame_size);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusEncoder::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &GodotOpusEncoder::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpusEncoder::get_channels);
}

void GodotOpusEncoder::initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms) {
	const int error = encoder.init(p_sample_rate, p_channels);
	if (error != OPUS_OK) {
		UtilityFunctions::push_error("GodotOpusEncoder: failed to create encoder (", p_sample_rate, " Hz, ", p_channels, " channels): ", opus_strerror(error));
		return;
	}
	frame_size = godotopus::frame_samples(p_sample_rate, checked_frame_duration_ms(p_frame_duration_ms));
}

PackedByteArray GodotOpusEncoder::encode(const PackedFloat32Array &p_pcm) {
	PackedByteArray result;
	if (!encoder.is_initialized()) {
		UtilityFunctions::push_error("GodotOpusEncoder: not initialized.");
		return result;
	}
	const int expected = frame_size * encoder.get_channels();
	if (p_pcm.size() != expected) {
		UtilityFunctions::push_error("GodotOpusEncoder: expected ", expected, " samples, got ", p_pcm.size(), ".");
		return result;
	}

	const int bytes = encoder.encode(p_pcm.ptr(), frame_size, packet);
	if (bytes < 0) {
		UtilityFunctions::push_error("GodotOpusEncoder: encode failed: ", opus_strerror(bytes));
		return result;
	}
	result.resize(bytes);
	std::memcpy(result.ptrw(), packet.data(), bytes);
	return result;
}

void GodotOpusEncoder::set_bitrate(int p_bitrate) {
	encoder.set_bitrate(p_bitrate);
}

int GodotOpusEncoder::get_bitrate() const {
	return encoder.get_bitrate();
}

void GodotOpusEncoder::set_complexity(int p_complexity) {
	encoder.set_complexity(p_complexity);
}

void GodotOpusEncoder::set_vbr(bool p_enabled, bool p_constrained) {
	encoder.set_vbr(p_enabled, p_constrained);
}

void GodotOpusEncoder::set_signal_voice(bool p_enabled) {
	encoder.set_signal_voice(p_enabled);
}

void GodotOpusEncoder::set_inband_fec(bool p_enabled) {
	encoder.set_inband_fec(p_enabled);
}

void GodotOpusEncoder::set_expected_packet_loss(int p_percent) {
	encoder.set_expected_packet_loss(p_percent);
}

void GodotOpusEncoder::set_dtx(bool p_enabled) {
	encoder.set_dtx(p_enabled);
}

bool GodotOpusEncoder::is_in_dtx() const {
	return encoder.is_in_dtx();
}

void GodotOpusEncoder::set_dred_duration_ms(int p_duration_ms) {
	encoder.set_dred_duration_ms(p_duration_ms);
}

int GodotOpusEncoder::get_dred_duration_ms() const {
	return encoder.get_dred_duration_ms();
}

bool GodotOpusEncoder::has_dred_support() const {
	return encoder.supports_dred();
}

void GodotOpusEncoder::reset() {
	encoder.reset();
}

void GodotOpusEncoder::set_frame_size(int p_frame_size) {
	if (!godotopus::is_valid_frame_samples(encoder.get_sample_rate(), p_frame_size)) {
		UtilityFunctions::push_error("GodotOpusEncoder: ", p_frame_size, " samples is not a valid Opus frame size at ", encoder.get_sample_rate(), " Hz.");
		return;
	}
	frame_size = p_frame_size;
}

int GodotOpusEncoder::get_frame_size() const {
	return frame_size;
}

int GodotOpusEncoder::get_sample_rate() const {
	return encoder.get_sample_rate();
}

int GodotOpusEncoder::get_channels() const {
	return encoder.get_channels();
}

// GodotOpusDecoder

void GodotOpusDecoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels", "frame_duration_ms"), &GodotOpusDecoder::initialize, DEFVAL(20));
	ClassDB::bind_method(D_METHOD("decode", "packet"), &GodotOpusDecoder::decode);
	ClassDB::bind_method(D_METHOD("decode_plc"), &GodotOpusDecoder::decode_plc);
	ClassDB::bind_method(D_METHOD("decode_fec", "next_packet"), &GodotOpusDecoder::decode_fec);
	ClassDB::bind_method(D_METHOD("parse_dred", "packet"), &GodotOpusDecoder::parse_dred);
	ClassDB::bind_method(D_METHOD("decode_dred", "samples_back"), &GodotOpusDecoder::decode_dred);
	ClassDB::bind_method(D_METHOD("reset"), &GodotOpusDecoder::reset);

	ClassDB::bind_method(D_METHOD("set_complexity", "complexity"), &GodotOpusDecoder::set_complexity);
	ClassDB::bind_method(D_METHOD("get_complexity"), &GodotOpusDecoder::get_complexity);
	ClassDB::bind_method(D_METHOD("set_bandwidth_extension", "enabled"), &GodotOpusDecoder::set_bandwidth_extension);
	ClassDB::bind_method(D_METHOD("get_bandwidth_extension"), &GodotOpusDecoder::get_bandwidth_extension);
	ClassDB::bind_method(D_METHOD("has_bandwidth_extension_support"), &GodotOpusDecoder::has_bandwidth_extension_support);

	ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusDecoder::set_frame_size);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusDecoder::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &GodotOpusDecoder::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpusDecoder::get_channels);
}

void GodotOpusDecoder::initialize(int p_sample_rate, int p_channels, int p_frame_duration_ms) {
	const int error = decoder.init(p_sample_rate, p_channels);
	if (error != OPUS_OK) {
		UtilityFunctions::push_error("GodotOpusDecoder: failed to create decoder (", p_sample_rate, " Hz, ", p_channels, " channels): ", opus_strerror(error));
		return;
	}
	frame_size = godotopus::frame_samples(p_sample_rate, checked_frame_duration_ms(p_frame_duration_ms));
}

PackedFloat32Array GodotOpusDecoder::to_packed(int p_result, const char *p_what) {
	PackedFloat32Array result;
	if (p_result < 0) {
		UtilityFunctions::push_error("GodotOpusDecoder: ", p_what, " failed: ", opus_strerror(p_result));
		return result;
	}
	result.resize(static_cast<int64_t>(pcm.size()));
	if (!pcm.empty()) {
		std::memcpy(result.ptrw(), pcm.data(), pcm.size() * sizeof(float));
	}
	return result;
}

PackedFloat32Array GodotOpusDecoder::decode(const PackedByteArray &p_packet) {
	if (!decoder.is_initialized()) {
		UtilityFunctions::push_error("GodotOpusDecoder: not initialized.");
		return PackedFloat32Array();
	}
	if (p_packet.is_empty()) {
		return decode_plc();
	}
	return to_packed(decoder.decode(p_packet.ptr(), p_packet.size(), pcm), "decode");
}

PackedFloat32Array GodotOpusDecoder::decode_plc() {
	if (!decoder.is_initialized()) {
		UtilityFunctions::push_error("GodotOpusDecoder: not initialized.");
		return PackedFloat32Array();
	}
	return to_packed(decoder.conceal(frame_size, pcm), "concealment");
}

PackedFloat32Array GodotOpusDecoder::decode_fec(const PackedByteArray &p_next_packet) {
	if (!decoder.is_initialized()) {
		UtilityFunctions::push_error("GodotOpusDecoder: not initialized.");
		return PackedFloat32Array();
	}
	return to_packed(decoder.decode_fec(p_next_packet.ptr(), p_next_packet.size(), frame_size, pcm), "FEC decode");
}

int GodotOpusDecoder::parse_dred(const PackedByteArray &p_packet) {
	return decoder.parse_dred(p_packet.ptr(), p_packet.size());
}

PackedFloat32Array GodotOpusDecoder::decode_dred(int p_samples_back) {
	const int result = decoder.decode_dred(p_samples_back, frame_size, pcm);
	// Out-of-range requests are expected (the caller falls back to PLC), so
	// they return an empty array without an error.
	if (result < 0) {
		return PackedFloat32Array();
	}
	return to_packed(result, "DRED decode");
}

void GodotOpusDecoder::reset() {
	decoder.reset();
}

void GodotOpusDecoder::set_complexity(int p_complexity) {
	decoder.set_complexity(p_complexity);
}

int GodotOpusDecoder::get_complexity() const {
	return decoder.get_complexity();
}

void GodotOpusDecoder::set_bandwidth_extension(bool p_enabled) {
	decoder.set_bandwidth_extension(p_enabled);
}

bool GodotOpusDecoder::get_bandwidth_extension() const {
	return decoder.get_bandwidth_extension();
}

bool GodotOpusDecoder::has_bandwidth_extension_support() const {
	return decoder.supports_bandwidth_extension();
}

void GodotOpusDecoder::set_frame_size(int p_frame_size) {
	if (!godotopus::is_valid_frame_samples(decoder.get_sample_rate(), p_frame_size)) {
		UtilityFunctions::push_error("GodotOpusDecoder: ", p_frame_size, " samples is not a valid Opus frame size at ", decoder.get_sample_rate(), " Hz.");
		return;
	}
	frame_size = p_frame_size;
}

int GodotOpusDecoder::get_frame_size() const {
	return frame_size;
}

int GodotOpusDecoder::get_sample_rate() const {
	return decoder.get_sample_rate();
}

int GodotOpusDecoder::get_channels() const {
	return decoder.get_channels();
}
