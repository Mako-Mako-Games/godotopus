#include "voice_capture_pipeline.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <cstring>
#include <type_traits>

using namespace godot;

void VoiceCapturePipeline::_bind_methods() {
	ClassDB::bind_method(D_METHOD("process", "frames"), &VoiceCapturePipeline::process);
	ClassDB::bind_method(D_METHOD("reset"), &VoiceCapturePipeline::reset);
	ClassDB::bind_method(D_METHOD("is_speaking"), &VoiceCapturePipeline::is_speaking);
	ClassDB::bind_method(D_METHOD("get_level"), &VoiceCapturePipeline::get_level);
	ClassDB::bind_method(D_METHOD("get_stats"), &VoiceCapturePipeline::get_stats);
	ClassDB::bind_method(D_METHOD("has_dred_support"), &VoiceCapturePipeline::has_dred_support);

	ClassDB::bind_method(D_METHOD("set_input_mix_rate", "rate"), &VoiceCapturePipeline::set_input_mix_rate);
	ClassDB::bind_method(D_METHOD("get_input_mix_rate"), &VoiceCapturePipeline::get_input_mix_rate);
	ClassDB::bind_method(D_METHOD("set_sample_rate", "rate"), &VoiceCapturePipeline::set_sample_rate);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &VoiceCapturePipeline::get_sample_rate);
	ClassDB::bind_method(D_METHOD("set_channels", "channels"), &VoiceCapturePipeline::set_channels);
	ClassDB::bind_method(D_METHOD("get_channels"), &VoiceCapturePipeline::get_channels);
	ClassDB::bind_method(D_METHOD("set_frame_duration_ms", "ms"), &VoiceCapturePipeline::set_frame_duration_ms);
	ClassDB::bind_method(D_METHOD("get_frame_duration_ms"), &VoiceCapturePipeline::get_frame_duration_ms);
	ClassDB::bind_method(D_METHOD("set_bitrate", "bitrate"), &VoiceCapturePipeline::set_bitrate);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &VoiceCapturePipeline::get_bitrate);
	ClassDB::bind_method(D_METHOD("set_complexity", "complexity"), &VoiceCapturePipeline::set_complexity);
	ClassDB::bind_method(D_METHOD("get_complexity"), &VoiceCapturePipeline::get_complexity);
	ClassDB::bind_method(D_METHOD("set_inband_fec", "enabled"), &VoiceCapturePipeline::set_inband_fec);
	ClassDB::bind_method(D_METHOD("get_inband_fec"), &VoiceCapturePipeline::get_inband_fec);
	ClassDB::bind_method(D_METHOD("set_expected_packet_loss", "percent"), &VoiceCapturePipeline::set_expected_packet_loss);
	ClassDB::bind_method(D_METHOD("get_expected_packet_loss"), &VoiceCapturePipeline::get_expected_packet_loss);
	ClassDB::bind_method(D_METHOD("set_dtx", "enabled"), &VoiceCapturePipeline::set_dtx);
	ClassDB::bind_method(D_METHOD("get_dtx"), &VoiceCapturePipeline::get_dtx);
	ClassDB::bind_method(D_METHOD("set_dred_duration_ms", "ms"), &VoiceCapturePipeline::set_dred_duration_ms);
	ClassDB::bind_method(D_METHOD("get_dred_duration_ms"), &VoiceCapturePipeline::get_dred_duration_ms);
	ClassDB::bind_method(D_METHOD("set_max_backlog_ms", "ms"), &VoiceCapturePipeline::set_max_backlog_ms);
	ClassDB::bind_method(D_METHOD("get_max_backlog_ms"), &VoiceCapturePipeline::get_max_backlog_ms);
	ClassDB::bind_method(D_METHOD("set_speaking_timeout_ms", "ms"), &VoiceCapturePipeline::set_speaking_timeout_ms);
	ClassDB::bind_method(D_METHOD("get_speaking_timeout_ms"), &VoiceCapturePipeline::get_speaking_timeout_ms);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_mix_rate", PROPERTY_HINT_RANGE, "8000,384000,1,suffix:Hz"), "set_input_mix_rate", "get_input_mix_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sample_rate", PROPERTY_HINT_ENUM, "8000:8000,12000:12000,16000:16000,24000:24000,48000:48000"), "set_sample_rate", "get_sample_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "channels", PROPERTY_HINT_ENUM, "Mono:1,Stereo:2"), "set_channels", "get_channels");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_duration_ms", PROPERTY_HINT_ENUM, "5:5,10:10,20:20,40:40,60:60"), "set_frame_duration_ms", "get_frame_duration_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bitrate", PROPERTY_HINT_RANGE, "6000,510000,1000,suffix:b/s"), "set_bitrate", "get_bitrate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "complexity", PROPERTY_HINT_RANGE, "0,10,1"), "set_complexity", "get_complexity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inband_fec"), "set_inband_fec", "get_inband_fec");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "expected_packet_loss", PROPERTY_HINT_RANGE, "0,100,1,suffix:%"), "set_expected_packet_loss", "get_expected_packet_loss");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dtx"), "set_dtx", "get_dtx");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dred_duration_ms", PROPERTY_HINT_RANGE, "0,1000,10,suffix:ms"), "set_dred_duration_ms", "get_dred_duration_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_backlog_ms", PROPERTY_HINT_RANGE, "5,1000,1,suffix:ms"), "set_max_backlog_ms", "get_max_backlog_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "speaking_timeout_ms", PROPERTY_HINT_RANGE, "0,2000,1,suffix:ms"), "set_speaking_timeout_ms", "get_speaking_timeout_ms");
}

VoiceCapturePipeline::VoiceCapturePipeline() {
	settings = pipeline.get_settings();
}

void VoiceCapturePipeline::apply_settings() {
	pipeline.configure(settings);
	settings = pipeline.get_settings(); // validated
}

TypedArray<PackedByteArray> VoiceCapturePipeline::process(const PackedVector2Array &p_frames) {
	const size_t frames = static_cast<size_t>(p_frames.size());
	const float *stereo = nullptr;
	if constexpr (std::is_same_v<real_t, float>) {
		stereo = reinterpret_cast<const float *>(p_frames.ptr());
	} else {
		scratch.resize(frames * 2);
		for (size_t i = 0; i < frames; i++) {
			scratch[i * 2] = static_cast<float>(p_frames[i].x);
			scratch[i * 2 + 1] = static_cast<float>(p_frames[i].y);
		}
		stereo = scratch.data();
	}

	packets.clear();
	pipeline.push(stereo, frames, packets);

	TypedArray<PackedByteArray> result;
	result.resize(static_cast<int64_t>(packets.size()));
	for (size_t i = 0; i < packets.size(); i++) {
		PackedByteArray bytes;
		bytes.resize(static_cast<int64_t>(packets[i].size()));
		std::memcpy(bytes.ptrw(), packets[i].data(), packets[i].size());
		result[static_cast<int64_t>(i)] = bytes;
	}
	return result;
}

void VoiceCapturePipeline::reset() {
	pipeline.reset();
}

bool VoiceCapturePipeline::is_speaking() const {
	return pipeline.is_speaking();
}

float VoiceCapturePipeline::get_level() const {
	return pipeline.get_level();
}

Dictionary VoiceCapturePipeline::get_stats() const {
	const godotopus::CapturePipeline::Stats &stats = pipeline.get_stats();
	Dictionary result;
	result["frames_encoded"] = static_cast<int64_t>(stats.frames_encoded);
	result["packets_sent"] = static_cast<int64_t>(stats.packets_sent);
	result["frames_silent"] = static_cast<int64_t>(stats.frames_silent);
	result["dropped_ms"] = static_cast<double>(stats.samples_dropped) * 1000.0 / settings.sample_rate;
	result["encoder_restarts"] = static_cast<int64_t>(stats.encoder_restarts);
	result["errors"] = static_cast<int64_t>(stats.errors);
	return result;
}

bool VoiceCapturePipeline::has_dred_support() const {
	return pipeline.supports_dred();
}

void VoiceCapturePipeline::set_input_mix_rate(int p_rate) {
	settings.input_rate = p_rate;
	apply_settings();
}

int VoiceCapturePipeline::get_input_mix_rate() const {
	return settings.input_rate;
}

void VoiceCapturePipeline::set_sample_rate(int p_rate) {
	settings.sample_rate = p_rate;
	apply_settings();
}

int VoiceCapturePipeline::get_sample_rate() const {
	return settings.sample_rate;
}

void VoiceCapturePipeline::set_channels(int p_channels) {
	settings.channels = p_channels;
	apply_settings();
}

int VoiceCapturePipeline::get_channels() const {
	return settings.channels;
}

void VoiceCapturePipeline::set_frame_duration_ms(int p_ms) {
	settings.frame_duration_ms = p_ms;
	apply_settings();
}

int VoiceCapturePipeline::get_frame_duration_ms() const {
	return settings.frame_duration_ms;
}

void VoiceCapturePipeline::set_bitrate(int p_bitrate) {
	settings.bitrate = p_bitrate;
	apply_settings();
}

int VoiceCapturePipeline::get_bitrate() const {
	return settings.bitrate;
}

void VoiceCapturePipeline::set_complexity(int p_complexity) {
	settings.complexity = p_complexity;
	apply_settings();
}

int VoiceCapturePipeline::get_complexity() const {
	return settings.complexity;
}

void VoiceCapturePipeline::set_inband_fec(bool p_enabled) {
	settings.inband_fec = p_enabled;
	apply_settings();
}

bool VoiceCapturePipeline::get_inband_fec() const {
	return settings.inband_fec;
}

void VoiceCapturePipeline::set_expected_packet_loss(int p_percent) {
	settings.expected_loss_percent = p_percent;
	apply_settings();
}

int VoiceCapturePipeline::get_expected_packet_loss() const {
	return settings.expected_loss_percent;
}

void VoiceCapturePipeline::set_dtx(bool p_enabled) {
	settings.dtx = p_enabled;
	apply_settings();
}

bool VoiceCapturePipeline::get_dtx() const {
	return settings.dtx;
}

void VoiceCapturePipeline::set_dred_duration_ms(int p_ms) {
	settings.dred_duration_ms = p_ms;
	apply_settings();
}

int VoiceCapturePipeline::get_dred_duration_ms() const {
	return settings.dred_duration_ms;
}

void VoiceCapturePipeline::set_max_backlog_ms(int p_ms) {
	settings.max_backlog_ms = p_ms;
	apply_settings();
}

int VoiceCapturePipeline::get_max_backlog_ms() const {
	return settings.max_backlog_ms;
}

void VoiceCapturePipeline::set_speaking_timeout_ms(int p_ms) {
	settings.speaking_timeout_ms = p_ms;
	apply_settings();
}

int VoiceCapturePipeline::get_speaking_timeout_ms() const {
	return settings.speaking_timeout_ms;
}
