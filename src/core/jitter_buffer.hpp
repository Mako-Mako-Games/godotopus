#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace godotopus {

// Reorders incoming packets by sequence number and decides, one frame at a
// time, what the receiver should play next. It doesn't decode anything: the
// caller turns each Event into audio (see VoiceSource), which keeps this
// class deterministic and cheap enough to run under a lock.
//
// Sequence numbers are 16-bit and count transmitted packets, so a gap in
// them means packets were lost. They may wrap around.
//
// Playback works in talk spurts. While idle the buffer collects packets and
// starts playing once it holds the target depth, or once the oldest packet
// has waited that long (so a short utterance isn't held back). While playing
// it hands out one frame per pop(): a packet, or a concealment step for a
// missing one. When it runs dry it goes idle and primes again, which also
// covers the sender going quiet (silent frames aren't transmitted).
class JitterBuffer {
public:
	struct Settings {
		// Audio to buffer before playing, in samples at 48 kHz.
		int target_samples = 2880; // 60 ms
		// Buffered audio beyond this is trimmed back to the target.
		int max_samples = 9600; // 200 ms
		// Longest run of missing frames to conceal. A longer gap is skipped.
		int max_conceal_samples = 9600; // 200 ms
	};

	enum class EventType {
		NONE,	 // nothing to play: the caller plays silence
		PACKET,	 // decode `packet`
		CONCEAL, // fill one missing frame; `packet` is the next one received
	};

	struct Event {
		EventType type = EventType::NONE;
		uint16_t seq = 0; // of `packet`
		// Bytes of the packet to decode, or for CONCEAL the next packet
		// received after the gap (its FEC and DRED data cover the gap).
		std::vector<uint8_t> packet;
		// CONCEAL: samples per missing frame, and how many frames before
		// `packet` the missing one is (1 = the frame right before it).
		int frame_samples = 0;
		int frames_before = 0;
		// The audio before this event wasn't continuous with it (silence,
		// a restart or skipped packets); the caller should fade in.
		bool resumed = false;
	};

	struct Stats {
		uint64_t packets_received = 0;
		uint64_t packets_played = 0;
		// Arrived after their slot was played or concealed, or duplicates.
		uint64_t packets_late = 0;
		// Thrown away to bring the buffer back down to the target depth.
		uint64_t packets_dropped = 0;
		// Missing packets skipped rather than concealed (long gaps, gaps
		// at the start of a talk spurt).
		uint64_t packets_skipped = 0;
		uint64_t frames_concealed = 0;
		uint64_t underruns = 0; // ran dry while playing
		uint64_t resyncs = 0;	// sender restarts detected
		int depth_samples = 0;	// audio currently buffered
	};

	// Sequence distance the buffer can track at once.
	static constexpr int CAPACITY = 256;
	// Late packets within this distance are dropped as reordered; further
	// back means the sender restarted.
	static constexpr int REORDER_WINDOW = 16;
	// This many late packets in a row also mean the sender restarted.
	static constexpr int LATE_RESYNC_COUNT = 3;

	JitterBuffer();

	void set_settings(const Settings &p_settings);
	const Settings &get_settings() const { return settings; }

	// Stores a packet. p_samples is its duration at 48 kHz (inspect_packet);
	// packets that don't parse should not be inserted.
	void insert(uint16_t p_seq, const uint8_t *p_data, size_t p_size, int p_samples);

	// Fills r_event with what to play next. When it returns NONE the caller
	// plays p_idle_samples of silence, which counts as time waited. Reuses
	// r_event's packet storage, so it doesn't allocate once warmed up.
	void pop(Event &r_event, int p_idle_samples);

	// Drops everything and returns to idle. Stats are kept.
	void clear();

	bool is_playing() const { return playing; }
	Stats get_stats() const;

private:
	struct Slot {
		bool filled = false;
		uint16_t seq = 0;
		int samples = 0;
		std::vector<uint8_t> data;
	};

	Slot &slot(uint16_t p_seq) { return slots[p_seq % CAPACITY]; }
	static int distance(uint16_t p_from, uint16_t p_to) { return static_cast<int16_t>(static_cast<uint16_t>(p_to - p_from)); }

	void store(uint16_t p_seq, const uint8_t *p_data, size_t p_size, int p_samples);
	void restart_at(uint16_t p_seq);
	// Advances past the head slot, counting a stored packet as dropped and
	// an empty slot as skipped.
	void release_head();
	// Distance from head to the next filled slot, or -1 if none.
	int next_filled_distance() const;
	void take_head(Event &r_event);

	Settings settings;
	std::array<Slot, CAPACITY> slots;
	Stats stats;

	bool anchored = false; // `head` is meaningful
	bool playing = false;
	bool resuming = false; // the next event follows a discontinuity
	// Nothing has played since anchoring, so reordered earlier packets can
	// still move the head back.
	bool fresh = false;
	uint16_t head = 0;	 // next sequence number to play
	uint16_t newest = 0; // highest sequence number stored
	int buffered = 0;	 // packets stored
	int buffered_samples = 0;
	int waited_samples = 0; // idle time with packets waiting
	int last_samples = 0;	// duration of the last packet played
	int late_streak = 0;
};

} // namespace godotopus
