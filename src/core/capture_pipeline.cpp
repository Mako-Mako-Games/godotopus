#include "capture_pipeline.hpp"

#include "dsp.hpp"
#include "frame.hpp"

#include <algorithm>
#include <cstdlib>

namespace godotopus {

namespace {

constexpr int MIN_INPUT_RATE = 8000;
constexpr int MAX_INPUT_RATE = 384000;

// The valid value closest to p_value (the first one on a tie).
template <size_t N>
int snap(int p_value, const int (&p_valid)[N]) {
	int best = p_valid[0];
	for (int candidate : p_valid) {
		if (std::abs(candidate - p_value) < std::abs(best - p_value)) {
			best = candidate;
		}
	}
	return best;
}

CapturePipeline::Settings validated(const CapturePipeline::Settings &p_settings) {
	static const int sample_rates[] = { 48000, 24000, 16000, 12000, 8000 };
	static const int frame_durations[] = { 20, 10, 40, 60, 5 };

	CapturePipeline::Settings s = p_settings;
	s.input_rate = std::clamp(s.input_rate, MIN_INPUT_RATE, MAX_INPUT_RATE);
	s.sample_rate = snap(s.sample_rate, sample_rates);
	s.channels = s.channels == 2 ? 2 : 1;
	s.frame_duration_ms = snap(s.frame_duration_ms, frame_durations);
	s.bitrate = std::clamp(s.bitrate, 6000, 510000);
	s.complexity = std::clamp(s.complexity, 0, 10);
	s.expected_loss_percent = std::clamp(s.expected_loss_percent, 0, 100);
	s.dred_duration_ms = std::clamp(s.dred_duration_ms, 0, 1000);
	// Must hold at least one frame, or nothing would ever be encoded.
	s.max_backlog_ms = std::max(s.max_backlog_ms, s.frame_duration_ms);
	s.speaking_timeout_ms = std::max(0, s.speaking_timeout_ms);
	return s;
}

} // namespace

CapturePipeline::CapturePipeline() {
	settings = validated(Settings());
}

void CapturePipeline::configure(const Settings &p_settings) {
	const Settings next = validated(p_settings);
	if (next.sample_rate != settings.sample_rate || next.channels != settings.channels || next.frame_duration_ms != settings.frame_duration_ms) {
		restart_pending = true;
	}
	if (next.input_rate != settings.input_rate) {
		resampler_pending = true;
	}
	settings = next;
	if (!restart_pending) {
		apply_codec_settings();
	}
}

void CapturePipeline::apply_codec_settings() {
	if (!encoder.is_initialized()) {
		return;
	}
	encoder.set_bitrate(settings.bitrate);
	encoder.set_complexity(settings.complexity);
	encoder.set_inband_fec(settings.inband_fec);
	encoder.set_expected_packet_loss(settings.expected_loss_percent);
	encoder.set_dtx(settings.dtx);
	// OPUS_UNIMPLEMENTED on light builds; nothing to fall back to.
	encoder.set_dred_duration_ms(settings.dred_duration_ms);
}

bool CapturePipeline::ensure_started() {
	if (restart_pending) {
		const bool restarting = encoder.is_initialized();
		if (encoder.init(settings.sample_rate, settings.channels) != OPUS_OK) {
			stats.errors++;
			return false;
		}
		if (restarting) {
			stats.encoder_restarts++;
		}
		apply_codec_settings();
		frame_size = frame_samples(settings.sample_rate, settings.frame_duration_ms);
		pending.clear();
		restart_pending = false;
		resampler_pending = true; // its channel count or output rate may have changed
	}
	if (resampler_pending) {
		if (settings.input_rate == settings.sample_rate) {
			resampler.release();
		} else if (resampler.init(settings.input_rate, settings.sample_rate, settings.channels) != 0) {
			stats.errors++;
			return false;
		}
		resampler_pending = false;
	}
	return true;
}

int CapturePipeline::push(const float *p_stereo, size_t p_frames, std::vector<Packet> &r_packets) {
	if (!p_stereo || p_frames == 0 || !ensure_started()) {
		return 0;
	}
	const int channels = settings.channels;

	converted.resize(p_frames * channels);
	if (channels == 1) {
		dsp::downmix_to_mono(p_stereo, p_frames, converted.data());
	} else {
		std::copy_n(p_stereo, p_frames * 2, converted.data());
	}
	level = dsp::rms(converted.data(), converted.size());

	const std::vector<float> *input = &converted;
	if (resampler.is_initialized()) {
		if (resampler.process(converted.data(), p_frames, resampled) != 0) {
			stats.errors++;
			return 0;
		}
		input = &resampled;
	}
	pending.insert(pending.end(), input->begin(), input->end());

	const size_t max_backlog = static_cast<size_t>(settings.sample_rate / 1000 * settings.max_backlog_ms);
	const size_t pending_frames = pending.size() / channels;
	if (pending_frames > max_backlog) {
		const size_t drop = pending_frames - max_backlog;
		pending.erase(pending.begin(), pending.begin() + drop * channels);
		stats.samples_dropped += drop;
	}

	const size_t step = static_cast<size_t>(frame_size) * channels;
	const int silence_timeout = settings.sample_rate / 1000 * settings.speaking_timeout_ms;
	int appended = 0;
	size_t offset = 0;
	for (; offset + step <= pending.size(); offset += step) {
		const int bytes = encoder.encode(pending.data() + offset, frame_size, packet);
		if (bytes < 0) {
			stats.errors++;
			continue;
		}
		stats.frames_encoded++;
		// Gating on the DTX state rather than only on packet size also
		// holds back the comfort-noise updates Opus emits every ~400 ms
		// of silence.
		if (is_dtx_packet(packet.size()) || encoder.is_in_dtx()) {
			stats.frames_silent++;
			silent_samples = std::min(silent_samples + frame_size, silence_timeout + frame_size);
			if (silent_samples >= silence_timeout) {
				speaking = false;
			}
			continue;
		}
		r_packets.push_back(packet);
		stats.packets_sent++;
		appended++;
		silent_samples = 0;
		speaking = true;
	}
	pending.erase(pending.begin(), pending.begin() + offset);
	return appended;
}

void CapturePipeline::reset() {
	pending.clear();
	if (encoder.is_initialized()) {
		encoder.reset();
	}
	if (resampler.is_initialized()) {
		resampler.reset();
	}
	level = 0.0f;
	speaking = false;
	silent_samples = 0;
}

bool CapturePipeline::supports_dred() const {
	if (encoder.is_initialized()) {
		return encoder.supports_dred();
	}
	Encoder probe;
	return probe.init(48000, 1) == OPUS_OK && probe.supports_dred();
}

} // namespace godotopus
