#include "voice_stream.hpp"

#include "core/frame.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

using namespace godot;

namespace {

constexpr int SAMPLES_PER_MS = godotopus::DECODE_SAMPLE_RATE / 1000;

static_assert(sizeof(AudioFrame) == 2 * sizeof(float), "AudioFrame must be an interleaved stereo pair");

int ms_to_samples(int p_ms) {
	return std::max(0, p_ms) * SAMPLES_PER_MS;
}

int samples_to_ms(int p_samples) {
	return p_samples / SAMPLES_PER_MS;
}

} // namespace

// VoiceStream

void VoiceStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("push_packet", "packet", "sequence"), &VoiceStream::push_packet);
	ClassDB::bind_method(D_METHOD("reset"), &VoiceStream::reset);
	ClassDB::bind_method(D_METHOD("get_stats"), &VoiceStream::get_stats);
	ClassDB::bind_method(D_METHOD("get_level"), &VoiceStream::get_level);
	ClassDB::bind_method(D_METHOD("is_speaking"), &VoiceStream::is_speaking);

	ClassDB::bind_method(D_METHOD("set_target_latency_ms", "ms"), &VoiceStream::set_target_latency_ms);
	ClassDB::bind_method(D_METHOD("get_target_latency_ms"), &VoiceStream::get_target_latency_ms);
	ClassDB::bind_method(D_METHOD("set_max_latency_ms", "ms"), &VoiceStream::set_max_latency_ms);
	ClassDB::bind_method(D_METHOD("get_max_latency_ms"), &VoiceStream::get_max_latency_ms);
	ClassDB::bind_method(D_METHOD("set_max_conceal_ms", "ms"), &VoiceStream::set_max_conceal_ms);
	ClassDB::bind_method(D_METHOD("get_max_conceal_ms"), &VoiceStream::get_max_conceal_ms);
	ClassDB::bind_method(D_METHOD("set_channels", "channels"), &VoiceStream::set_channels);
	ClassDB::bind_method(D_METHOD("get_channels"), &VoiceStream::get_channels);
	ClassDB::bind_method(D_METHOD("set_decoder_complexity", "complexity"), &VoiceStream::set_decoder_complexity);
	ClassDB::bind_method(D_METHOD("get_decoder_complexity"), &VoiceStream::get_decoder_complexity);
	ClassDB::bind_method(D_METHOD("set_bandwidth_extension", "enabled"), &VoiceStream::set_bandwidth_extension);
	ClassDB::bind_method(D_METHOD("get_bandwidth_extension"), &VoiceStream::get_bandwidth_extension);
	ClassDB::bind_method(D_METHOD("set_speaking_timeout_ms", "ms"), &VoiceStream::set_speaking_timeout_ms);
	ClassDB::bind_method(D_METHOD("get_speaking_timeout_ms"), &VoiceStream::get_speaking_timeout_ms);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "target_latency_ms", PROPERTY_HINT_RANGE, "0,500,1,suffix:ms"), "set_target_latency_ms", "get_target_latency_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_latency_ms", PROPERTY_HINT_RANGE, "20,2000,1,suffix:ms"), "set_max_latency_ms", "get_max_latency_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_conceal_ms", PROPERTY_HINT_RANGE, "0,1000,1,suffix:ms"), "set_max_conceal_ms", "get_max_conceal_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "channels", PROPERTY_HINT_ENUM, "Mono:1,Stereo:2"), "set_channels", "get_channels");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "decoder_complexity", PROPERTY_HINT_RANGE, "0,10,1"), "set_decoder_complexity", "get_decoder_complexity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bandwidth_extension"), "set_bandwidth_extension", "get_bandwidth_extension");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "speaking_timeout_ms", PROPERTY_HINT_RANGE, "0,2000,1,suffix:ms"), "set_speaking_timeout_ms", "get_speaking_timeout_ms");
}

VoiceStream::VoiceStream() {
	settings = source.get_settings();
}

void VoiceStream::apply_settings() {
	source.configure(settings);
	settings = source.get_settings(); // validated
}

void VoiceStream::push_packet(const PackedByteArray &p_packet, int p_sequence) {
	source.push_packet(static_cast<uint16_t>(p_sequence & 0xffff), p_packet.ptr(), static_cast<size_t>(p_packet.size()));
}

void VoiceStream::reset() {
	source.reset();
}

Dictionary VoiceStream::get_stats() const {
	const godotopus::VoiceSource::Stats stats = source.get_stats();
	Dictionary result;
	result["packets_received"] = static_cast<int64_t>(stats.jitter.packets_received);
	result["packets_played"] = static_cast<int64_t>(stats.jitter.packets_played);
	result["packets_late"] = static_cast<int64_t>(stats.jitter.packets_late);
	result["packets_dropped"] = static_cast<int64_t>(stats.jitter.packets_dropped);
	result["packets_skipped"] = static_cast<int64_t>(stats.jitter.packets_skipped);
	result["frames_concealed"] = static_cast<int64_t>(stats.jitter.frames_concealed);
	result["frames_dred"] = static_cast<int64_t>(stats.frames_dred);
	result["frames_fec"] = static_cast<int64_t>(stats.frames_fec);
	result["frames_plc"] = static_cast<int64_t>(stats.frames_plc);
	result["underruns"] = static_cast<int64_t>(stats.jitter.underruns);
	result["resyncs"] = static_cast<int64_t>(stats.jitter.resyncs);
	result["decode_errors"] = static_cast<int64_t>(stats.decode_errors);
	result["depth_ms"] = static_cast<double>(stats.jitter.depth_samples) / SAMPLES_PER_MS;
	return result;
}

float VoiceStream::get_level() const {
	return source.get_level();
}

bool VoiceStream::is_speaking() const {
	return source.is_speaking();
}

void VoiceStream::set_target_latency_ms(int p_ms) {
	settings.jitter.target_samples = ms_to_samples(p_ms);
	settings.jitter.max_samples = std::max(settings.jitter.max_samples, settings.jitter.target_samples);
	apply_settings();
}

int VoiceStream::get_target_latency_ms() const {
	return samples_to_ms(settings.jitter.target_samples);
}

void VoiceStream::set_max_latency_ms(int p_ms) {
	settings.jitter.max_samples = ms_to_samples(p_ms);
	apply_settings();
}

int VoiceStream::get_max_latency_ms() const {
	return samples_to_ms(settings.jitter.max_samples);
}

void VoiceStream::set_max_conceal_ms(int p_ms) {
	settings.jitter.max_conceal_samples = ms_to_samples(p_ms);
	apply_settings();
}

int VoiceStream::get_max_conceal_ms() const {
	return samples_to_ms(settings.jitter.max_conceal_samples);
}

void VoiceStream::set_channels(int p_channels) {
	settings.channels = p_channels;
	apply_settings();
}

int VoiceStream::get_channels() const {
	return settings.channels;
}

void VoiceStream::set_decoder_complexity(int p_complexity) {
	settings.complexity = p_complexity;
	apply_settings();
}

int VoiceStream::get_decoder_complexity() const {
	return settings.complexity;
}

void VoiceStream::set_bandwidth_extension(bool p_enabled) {
	settings.bandwidth_extension = p_enabled;
	apply_settings();
}

bool VoiceStream::get_bandwidth_extension() const {
	return settings.bandwidth_extension;
}

void VoiceStream::set_speaking_timeout_ms(int p_ms) {
	settings.speaking_timeout_samples = ms_to_samples(p_ms);
	apply_settings();
}

int VoiceStream::get_speaking_timeout_ms() const {
	return samples_to_ms(settings.speaking_timeout_samples);
}

Ref<AudioStreamPlayback> VoiceStream::_instantiate_playback() const {
	Ref<VoiceStreamPlayback> playback;
	playback.instantiate();
	// The engine hands out const streams; playbacks only use the source's
	// thread-safe API.
	playback->stream = Ref<VoiceStream>(const_cast<VoiceStream *>(this));
	return playback;
}

String VoiceStream::_get_stream_name() const {
	return "VoiceStream";
}

double VoiceStream::_get_length() const {
	return 0.0; // live
}

bool VoiceStream::_is_monophonic() const {
	return false; // several players may share one voice
}

// VoiceStreamPlayback

void VoiceStreamPlayback::_start(double p_from_pos) {
	cursor = stream->get_source().live_cursor();
	active = true;
	// Primes the resampler, which mixes a little audio right away.
	begin_resample();
}

void VoiceStreamPlayback::_stop() {
	active = false;
}

bool VoiceStreamPlayback::_is_playing() const {
	return active;
}

int32_t VoiceStreamPlayback::_get_loop_count() const {
	return 0;
}

double VoiceStreamPlayback::_get_playback_position() const {
	return 0.0; // a live stream has no position
}

void VoiceStreamPlayback::_seek(double p_position) {
}

int32_t VoiceStreamPlayback::_mix_resampled(AudioFrame *p_dst_buffer, int32_t p_frame_count) {
	if (!active || stream.is_null()) {
		std::fill_n(reinterpret_cast<float *>(p_dst_buffer), static_cast<size_t>(p_frame_count) * 2, 0.0f);
		return p_frame_count;
	}
	stream->get_source().mix(cursor, reinterpret_cast<float *>(p_dst_buffer), p_frame_count);
	return p_frame_count;
}

float VoiceStreamPlayback::_get_stream_sampling_rate() const {
	return static_cast<float>(godotopus::DECODE_SAMPLE_RATE);
}
