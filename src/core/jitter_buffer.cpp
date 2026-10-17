#include "jitter_buffer.hpp"

#include <algorithm>

namespace godotopus {

JitterBuffer::JitterBuffer() {
	set_settings(Settings());
}

void JitterBuffer::set_settings(const Settings &p_settings) {
	settings.target_samples = std::max(0, p_settings.target_samples);
	settings.max_samples = std::max(settings.target_samples, p_settings.max_samples);
	settings.max_conceal_samples = std::max(0, p_settings.max_conceal_samples);
}

JitterBuffer::Stats JitterBuffer::get_stats() const {
	Stats result = stats;
	result.depth_samples = buffered_samples;
	return result;
}

void JitterBuffer::clear() {
	for (Slot &s : slots) {
		s.filled = false; // keeps the data's capacity for reuse
	}
	anchored = false;
	playing = false;
	resuming = false;
	fresh = false;
	buffered = 0;
	buffered_samples = 0;
	waited_samples = 0;
	last_samples = 0;
	late_streak = 0;
}

void JitterBuffer::restart_at(uint16_t p_seq) {
	stats.packets_dropped += buffered;
	clear();
	anchored = true;
	resuming = true;
	fresh = true;
	head = p_seq;
	newest = p_seq;
}

void JitterBuffer::store(uint16_t p_seq, const uint8_t *p_data, size_t p_size, int p_samples) {
	Slot &s = slot(p_seq);
	s.filled = true;
	s.seq = p_seq;
	s.samples = p_samples;
	s.data.assign(p_data, p_data + p_size);
	if (buffered == 0 || distance(newest, p_seq) > 0) {
		newest = p_seq;
	}
	buffered++;
	buffered_samples += p_samples;
}

void JitterBuffer::release_head() {
	Slot &s = slot(head);
	if (s.filled && s.seq == head) {
		s.filled = false;
		buffered--;
		buffered_samples -= s.samples;
		stats.packets_dropped++;
	} else {
		stats.packets_skipped++;
	}
	head++;
}

int JitterBuffer::next_filled_distance() const {
	if (buffered == 0) {
		return -1;
	}
	for (int d = 0; d < CAPACITY; d++) {
		const uint16_t seq = static_cast<uint16_t>(head + d);
		const Slot &s = slots[seq % CAPACITY];
		if (s.filled && s.seq == seq) {
			return d;
		}
	}
	return -1;
}

void JitterBuffer::insert(uint16_t p_seq, const uint8_t *p_data, size_t p_size, int p_samples) {
	stats.packets_received++;
	if (!p_data || p_size == 0 || p_samples <= 0) {
		return;
	}
	if (!anchored) {
		restart_at(p_seq);
		store(p_seq, p_data, p_size, p_samples);
		return;
	}

	const int d = distance(head, p_seq);
	if (d < 0) {
		// Reordered packets that beat the stream's start can still be used.
		if (fresh && !playing && d >= -REORDER_WINDOW && distance(p_seq, newest) < CAPACITY) {
			head = p_seq;
			store(p_seq, p_data, p_size, p_samples);
			return;
		}
		// Far behind, or late again and again: the sender started over.
		if (d < -REORDER_WINDOW || ++late_streak >= LATE_RESYNC_COUNT) {
			stats.resyncs++;
			restart_at(p_seq);
			store(p_seq, p_data, p_size, p_samples);
			return;
		}
		stats.packets_late++;
		return;
	}
	late_streak = 0;

	if (d >= CAPACITY) {
		// Further ahead than anything we'd conceal: start over from here.
		stats.resyncs++;
		restart_at(p_seq);
		store(p_seq, p_data, p_size, p_samples);
		return;
	}

	const Slot &existing = slot(p_seq);
	if (existing.filled && existing.seq == p_seq) {
		stats.packets_late++; // duplicate
		return;
	}
	store(p_seq, p_data, p_size, p_samples);

	if (buffered_samples > settings.max_samples) {
		// A burst arrived (the sender or this process stalled). Draining it
		// at normal speed would keep the extra latency, so drop the oldest.
		while (buffered > 0 && buffered_samples > settings.target_samples) {
			release_head();
		}
		resuming = true;
	}
}

void JitterBuffer::take_head(Event &r_event) {
	Slot &s = slot(head);
	r_event.type = EventType::PACKET;
	r_event.seq = s.seq;
	r_event.packet.assign(s.data.begin(), s.data.end());
	r_event.frame_samples = s.samples;
	r_event.frames_before = 0;
	r_event.resumed = resuming;

	resuming = false;
	fresh = false;
	last_samples = s.samples;
	s.filled = false;
	buffered--;
	buffered_samples -= s.samples;
	head++;
	stats.packets_played++;
}

void JitterBuffer::pop(Event &r_event, int p_idle_samples) {
	r_event.type = EventType::NONE;
	r_event.resumed = false;
	r_event.frames_before = 0;

	if (!playing) {
		if (buffered == 0) {
			waited_samples = 0;
			return;
		}
		if (buffered_samples < settings.target_samples && waited_samples < settings.target_samples) {
			waited_samples += std::max(0, p_idle_samples);
			return;
		}
		// Start the talk spurt at the oldest packet. Anything missing before
		// it was due while we were idle, so it's too late to conceal.
		const int first = next_filled_distance();
		for (int i = 0; i < first; i++) {
			release_head();
		}
		playing = true;
		resuming = true;
		waited_samples = 0;
	}

	const int next = next_filled_distance();
	if (next == 0) {
		take_head(r_event);
		return;
	}
	if (next < 0) {
		// Ran dry: packets are late, lost, or the sender went quiet.
		playing = false;
		stats.underruns++;
		return;
	}

	const Slot &after_gap = slot(static_cast<uint16_t>(head + next));
	const int frame = last_samples > 0 ? last_samples : after_gap.samples;
	if (static_cast<long long>(next) * frame > settings.max_conceal_samples) {
		for (int i = 0; i < next; i++) {
			release_head();
		}
		resuming = true;
		take_head(r_event);
		return;
	}

	r_event.type = EventType::CONCEAL;
	r_event.seq = after_gap.seq;
	r_event.packet.assign(after_gap.data.begin(), after_gap.data.end());
	r_event.frame_samples = frame;
	r_event.frames_before = next;
	r_event.resumed = resuming;
	resuming = false;
	fresh = false;
	head++;
	stats.frames_concealed++;
}

} // namespace godotopus
