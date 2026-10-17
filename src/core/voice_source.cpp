#include "voice_source.hpp"

#include "frame.hpp"

#include <algorithm>

namespace godotopus {

namespace {
// Upper bound on the frames rendered per pass of mix(), so one event's
// overshoot (up to a 120 ms packet) always fits in the ring.
constexpr int MAX_MIX_CHUNK = 4096;
static_assert(MAX_MIX_CHUNK + DECODE_SAMPLE_RATE / 1000 * MAX_PACKET_DURATION_MS + VoiceSource::MAX_READER_LAG <= VoiceSource::RING_FRAMES,
		"the ring must hold a lagging reader's window plus one decode's overshoot");
} // namespace

VoiceSource::VoiceSource() {
	configure(Settings());
}

void VoiceSource::configure(const Settings &p_settings) {
	std::lock_guard<std::mutex> mix_lock(mix_mutex);
	std::lock_guard<std::mutex> state_lock(state_mutex);

	settings = p_settings;
	settings.channels = p_settings.channels == 2 ? 2 : 1;
	settings.complexity = std::clamp(p_settings.complexity, 0, 10);
	settings.fade_in_samples = std::max(0, p_settings.fade_in_samples);
	settings.speaking_timeout_samples = std::max(0, p_settings.speaking_timeout_samples);
	jitter.set_settings(p_settings.jitter);
	settings.jitter = jitter.get_settings();

	if (!decoder.is_initialized() || decoder_channels != settings.channels) {
		decoder.init(DECODE_SAMPLE_RATE, settings.channels);
		decoder_channels = settings.channels;
		ring.assign(static_cast<size_t>(RING_FRAMES) * decoder_channels, 0.0f);
		reset_locked();
	}
	decoder.set_complexity(settings.complexity);
	// OPUS_UNIMPLEMENTED on light builds; there's nothing to fall back to.
	decoder.set_bandwidth_extension(settings.bandwidth_extension);
}

VoiceSource::Settings VoiceSource::get_settings() const {
	std::lock_guard<std::mutex> lock(state_mutex);
	return settings;
}

void VoiceSource::push_packet(uint16_t p_seq, const uint8_t *p_data, size_t p_size) {
	PacketInfo info;
	if (inspect_packet(p_data, p_size, DECODE_SAMPLE_RATE, info) != OPUS_OK || info.samples <= 0) {
		decode_errors.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	std::lock_guard<std::mutex> lock(state_mutex);
	jitter.insert(p_seq, p_data, p_size, info.samples);
}

VoiceSource::Cursor VoiceSource::live_cursor() const {
	std::lock_guard<std::mutex> lock(mix_mutex);
	return written;
}

void VoiceSource::mix(Cursor &r_cursor, float *p_out, int p_frames) {
	std::lock_guard<std::mutex> lock(mix_mutex);

	while (p_frames > 0) {
		const int chunk = std::min(p_frames, MAX_MIX_CHUNK);
		// New readers and ones that stopped mixing for a while join live.
		if (r_cursor > written || written - r_cursor > MAX_READER_LAG) {
			r_cursor = written;
		}

		while (written < r_cursor + chunk) {
			const int needed = static_cast<int>(r_cursor + chunk - written);
			{
				std::lock_guard<std::mutex> state_lock(state_mutex);
				jitter.pop(event, needed);
			}
			if (event.type == JitterBuffer::EventType::NONE) {
				append_silence(needed);
				break;
			}

			const int frames = render(event);
			if (event.resumed) {
				fade.start(settings.fade_in_samples);
			}
			fade.apply(pcm.data(), frames, decoder_channels);
			level.store(dsp::rms(pcm.data(), pcm.size()), std::memory_order_relaxed);
			append_to_ring(pcm.data(), frames);
			if (event.type == JitterBuffer::EventType::PACKET) {
				last_packet_end = written;
			}
		}

		for (int i = 0; i < chunk; i++) {
			const size_t pos = static_cast<size_t>((r_cursor + i) % RING_FRAMES) * decoder_channels;
			p_out[i * 2] = ring[pos];
			p_out[i * 2 + 1] = ring[pos + decoder_channels - 1];
		}
		r_cursor += chunk;
		p_out += static_cast<size_t>(chunk) * 2;
		p_frames -= chunk;
	}

	speaking.store(last_packet_end >= 0 && written - last_packet_end < settings.speaking_timeout_samples, std::memory_order_relaxed);
}

int VoiceSource::render(const JitterBuffer::Event &p_event) {
	int frames = 0;
	if (p_event.type == JitterBuffer::EventType::PACKET) {
		dred_parsed = false; // any later gap is followed by a different packet
		frames = decoder.decode(p_event.packet.data(), p_event.packet.size(), pcm);
		if (frames <= 0) {
			decode_errors.fetch_add(1, std::memory_order_relaxed);
			frames = decoder.conceal(p_event.frame_samples, pcm);
		}
	} else {
		frames = conceal(p_event);
	}

	if (frames <= 0) {
		// Keep time even if the decoder refused: play silence instead.
		frames = p_event.frame_samples > 0 ? p_event.frame_samples : frame_samples(DECODE_SAMPLE_RATE, 20);
		pcm.assign(static_cast<size_t>(frames) * decoder_channels, 0.0f);
	}
	return frames;
}

int VoiceSource::conceal(const JitterBuffer::Event &p_event) {
	const int frame = p_event.frame_samples;
	const int offset = p_event.frames_before * frame;

	// Deep REDundancy reaches furthest back, so try it first. The packet
	// after the gap is parsed once per gap, on its first (furthest back)
	// frame, and only as far back as the gap reaches.
	if (!dred_parsed || dred_seq != p_event.seq) {
		dred_available = decoder.parse_dred(p_event.packet.data(), p_event.packet.size(), offset);
		dred_seq = p_event.seq;
		dred_parsed = true;
	}
	if (offset <= dred_available) {
		const int frames = decoder.decode_dred(offset, frame, pcm);
		if (frames > 0) {
			frames_dred.fetch_add(1, std::memory_order_relaxed);
			return frames;
		}
	}

	// In-band FEC only covers the frame right before the packet.
	if (p_event.frames_before == 1) {
		const int frames = decoder.decode_fec(p_event.packet.data(), p_event.packet.size(), frame, pcm);
		if (frames > 0) {
			frames_fec.fetch_add(1, std::memory_order_relaxed);
			return frames;
		}
	}

	const int frames = decoder.conceal(frame, pcm);
	if (frames > 0) {
		frames_plc.fetch_add(1, std::memory_order_relaxed);
	}
	return frames;
}

void VoiceSource::append_to_ring(const float *p_pcm, int p_frames) {
	for (int i = 0; i < p_frames; i++) {
		const size_t pos = static_cast<size_t>((written + i) % RING_FRAMES) * decoder_channels;
		for (int c = 0; c < decoder_channels; c++) {
			ring[pos + c] = p_pcm[static_cast<size_t>(i) * decoder_channels + c];
		}
	}
	written += p_frames;
}

void VoiceSource::append_silence(int p_frames) {
	for (int i = 0; i < p_frames; i++) {
		const size_t pos = static_cast<size_t>((written + i) % RING_FRAMES) * decoder_channels;
		for (int c = 0; c < decoder_channels; c++) {
			ring[pos + c] = 0.0f;
		}
	}
	written += p_frames;
	level.store(0.0f, std::memory_order_relaxed);
}

void VoiceSource::reset() {
	std::lock_guard<std::mutex> mix_lock(mix_mutex);
	std::lock_guard<std::mutex> state_lock(state_mutex);
	reset_locked();
}

void VoiceSource::reset_locked() {
	jitter.clear();
	decoder.reset();
	fade.start(0);
	dred_parsed = false;
	dred_available = 0;
	last_packet_end = -1;
	level.store(0.0f, std::memory_order_relaxed);
	speaking.store(false, std::memory_order_relaxed);
}

VoiceSource::Stats VoiceSource::get_stats() const {
	Stats result;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		result.jitter = jitter.get_stats();
	}
	result.frames_dred = frames_dred.load(std::memory_order_relaxed);
	result.frames_fec = frames_fec.load(std::memory_order_relaxed);
	result.frames_plc = frames_plc.load(std::memory_order_relaxed);
	result.decode_errors = decode_errors.load(std::memory_order_relaxed);
	return result;
}

} // namespace godotopus
