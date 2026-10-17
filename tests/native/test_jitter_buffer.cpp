#include "core/jitter_buffer.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <vector>

using namespace godotopus;
using EventType = JitterBuffer::EventType;

namespace {

constexpr int FRAME = 960; // 20 ms at 48 kHz

// The buffer doesn't look inside packets, so tests store the sequence
// number as the payload and check it comes back out.
void insert(JitterBuffer &p_buffer, uint16_t p_seq, int p_samples = FRAME) {
	const uint8_t data[] = { static_cast<uint8_t>(p_seq & 0xff), static_cast<uint8_t>(p_seq >> 8) };
	p_buffer.insert(p_seq, data, sizeof(data), p_samples);
}

uint16_t payload_seq(const JitterBuffer::Event &p_event) {
	REQUIRE(p_event.packet.size() == 2);
	return static_cast<uint16_t>(p_event.packet[0] | (p_event.packet[1] << 8));
}

JitterBuffer::Event pop(JitterBuffer &p_buffer) {
	JitterBuffer::Event event;
	p_buffer.pop(event, FRAME);
	return event;
}

// Pops one event and checks it plays packet p_seq.
void expect_packet(JitterBuffer &p_buffer, uint16_t p_seq) {
	const JitterBuffer::Event event = pop(p_buffer);
	CAPTURE(p_seq);
	REQUIRE(event.type == EventType::PACKET);
	CHECK(event.seq == p_seq);
	CHECK(payload_seq(event) == p_seq);
}

// Pops one event and checks it conceals the frame p_frames_before ahead of
// packet p_next_seq.
void expect_conceal(JitterBuffer &p_buffer, uint16_t p_next_seq, int p_frames_before, int p_frame = FRAME) {
	const JitterBuffer::Event event = pop(p_buffer);
	CAPTURE(p_next_seq);
	CAPTURE(p_frames_before);
	REQUIRE(event.type == EventType::CONCEAL);
	CHECK(event.seq == p_next_seq);
	CHECK(payload_seq(event) == p_next_seq);
	CHECK(event.frames_before == p_frames_before);
	CHECK(event.frame_samples == p_frame);
}

void expect_none(JitterBuffer &p_buffer) {
	CHECK(pop(p_buffer).type == EventType::NONE);
}

void insert_range(JitterBuffer &p_buffer, uint16_t p_first, int p_count, std::initializer_list<uint16_t> p_skip = {}) {
	for (int i = 0; i < p_count; i++) {
		const uint16_t seq = static_cast<uint16_t>(p_first + i);
		bool skip = false;
		for (uint16_t s : p_skip) {
			skip = skip || s == seq;
		}
		if (!skip) {
			insert(p_buffer, seq);
		}
	}
}

} // namespace

TEST_CASE("jitter buffer primes to the target depth, then plays in order") {
	JitterBuffer buffer; // target 60 ms = 3 frames
	expect_none(buffer);
	insert(buffer, 0);
	insert(buffer, 1);
	expect_none(buffer);
	CHECK_FALSE(buffer.is_playing());
	CHECK(buffer.get_stats().depth_samples == 2 * FRAME);

	insert(buffer, 2);
	JitterBuffer::Event first = pop(buffer);
	REQUIRE(first.type == EventType::PACKET);
	CHECK(first.seq == 0);
	CHECK(first.resumed); // start of a talk spurt
	CHECK(buffer.is_playing());

	insert_range(buffer, 3, 7);
	for (uint16_t seq = 1; seq < 10; seq++) {
		const JitterBuffer::Event event = pop(buffer);
		REQUIRE(event.type == EventType::PACKET);
		CHECK(event.seq == seq);
		CHECK_FALSE(event.resumed);
	}
	const JitterBuffer::Stats stats = buffer.get_stats();
	CHECK(stats.packets_received == 10);
	CHECK(stats.packets_played == 10);
	CHECK(stats.frames_concealed == 0);
	CHECK(stats.depth_samples == 0);
}

TEST_CASE("a lone packet plays once it has waited the target depth") {
	JitterBuffer buffer;
	insert(buffer, 7);
	// Waits 3 * 960 = the 60 ms target, then plays without more packets.
	expect_none(buffer);
	expect_none(buffer);
	expect_none(buffer);
	expect_packet(buffer, 7);
}

TEST_CASE("single loss is concealed from the next packet") {
	JitterBuffer buffer;
	insert_range(buffer, 0, 6, { 2 });
	expect_packet(buffer, 0);
	expect_packet(buffer, 1);
	expect_conceal(buffer, 3, 1);
	expect_packet(buffer, 3);
	expect_packet(buffer, 4);
	expect_packet(buffer, 5);
	CHECK(buffer.get_stats().frames_concealed == 1);
}

TEST_CASE("burst loss is concealed frame by frame, counting down to the next packet") {
	JitterBuffer buffer;
	insert_range(buffer, 0, 7, { 2, 3, 4 });
	expect_packet(buffer, 0);
	expect_packet(buffer, 1);
	expect_conceal(buffer, 5, 3);
	expect_conceal(buffer, 5, 2);
	expect_conceal(buffer, 5, 1);
	expect_packet(buffer, 5);
	expect_packet(buffer, 6);
	CHECK(buffer.get_stats().frames_concealed == 3);
}

TEST_CASE("concealment uses the duration of the last packet played") {
	JitterBuffer buffer;
	insert(buffer, 0, 2880); // 60 ms
	insert(buffer, 2, 960);
	expect_packet(buffer, 0);
	expect_conceal(buffer, 2, 1, 2880);
	expect_packet(buffer, 2);
}

TEST_CASE("reordered packets play in sequence order") {
	JitterBuffer buffer;
	for (uint16_t seq : { 1, 0, 3, 2, 5, 4 }) {
		insert(buffer, seq);
	}
	for (uint16_t seq = 0; seq < 6; seq++) {
		expect_packet(buffer, seq);
	}
	CHECK(buffer.get_stats().frames_concealed == 0);
	CHECK(buffer.get_stats().packets_late == 0);
}

TEST_CASE("packets arriving after their slot are dropped as late") {
	JitterBuffer buffer;
	insert_range(buffer, 0, 5, { 2 });
	expect_packet(buffer, 0);
	expect_packet(buffer, 1);
	expect_conceal(buffer, 3, 1);
	insert(buffer, 2); // too late now
	expect_packet(buffer, 3);
	insert(buffer, 3); // duplicate
	expect_packet(buffer, 4);
	CHECK(buffer.get_stats().packets_late == 2);
	CHECK(buffer.get_stats().resyncs == 0);
}

TEST_CASE("sequence numbers wrap around at 65535") {
	JitterBuffer buffer;
	insert_range(buffer, 65533, 6, { 65535 });
	expect_packet(buffer, 65533);
	expect_packet(buffer, 65534);
	expect_conceal(buffer, 0, 1);
	expect_packet(buffer, 0);
	expect_packet(buffer, 1);
	expect_packet(buffer, 2);
	CHECK(buffer.get_stats().resyncs == 0);
}

TEST_CASE("a sender restart resyncs the buffer") {
	SUBCASE("far behind: immediate") {
		JitterBuffer buffer;
		insert_range(buffer, 1000, 4);
		expect_packet(buffer, 1000);
		insert(buffer, 0);
		CHECK(buffer.get_stats().resyncs == 1);
		CHECK(buffer.get_stats().packets_dropped == 3); // the old stream's leftovers
		insert(buffer, 1);
		insert(buffer, 2);
		expect_packet(buffer, 0);
		CHECK(buffer.get_stats().packets_late == 0);
	}
	SUBCASE("slightly behind: after a few late packets in a row") {
		JitterBuffer buffer;
		insert_range(buffer, 10, 3);
		for (uint16_t seq = 10; seq < 13; seq++) {
			expect_packet(buffer, seq);
		}
		insert_range(buffer, 0, 5);
		CHECK(buffer.get_stats().resyncs == 1);
		CHECK(buffer.get_stats().packets_late == JitterBuffer::LATE_RESYNC_COUNT - 1);
		expect_packet(buffer, JitterBuffer::LATE_RESYNC_COUNT - 1);
	}
	SUBCASE("far ahead") {
		JitterBuffer buffer;
		insert_range(buffer, 0, 3);
		expect_packet(buffer, 0);
		insert_range(buffer, 5000, 3);
		CHECK(buffer.get_stats().resyncs == 1);
		const JitterBuffer::Event event = pop(buffer);
		CHECK(event.type == EventType::PACKET);
		CHECK(event.seq == 5000);
		CHECK(event.resumed);
	}
}

TEST_CASE("a backlog burst is trimmed back to the target depth") {
	JitterBuffer buffer; // max 200 ms, target 60 ms
	// The 11th packet overflows 200 ms and trims to 3 (8..10); 8 more
	// overflow again at 18 and trim to 16..18; 19 then fits.
	insert_range(buffer, 0, 20);
	const JitterBuffer::Stats stats = buffer.get_stats();
	CHECK(stats.depth_samples == 4 * FRAME);
	CHECK(stats.packets_dropped == 16);
	const JitterBuffer::Event event = pop(buffer);
	REQUIRE(event.type == EventType::PACKET);
	CHECK(event.seq == 16);
	CHECK(event.resumed);
	expect_packet(buffer, 17);
	expect_packet(buffer, 18);
	expect_packet(buffer, 19);
}

TEST_CASE("running dry ends the talk spurt and the buffer primes again") {
	JitterBuffer buffer;
	insert_range(buffer, 0, 3);
	for (uint16_t seq = 0; seq < 3; seq++) {
		expect_packet(buffer, seq);
	}
	expect_none(buffer);
	CHECK_FALSE(buffer.is_playing());
	CHECK(buffer.get_stats().underruns == 1);

	SUBCASE("the next spurt waits for the target depth") {
		insert(buffer, 3);
		expect_none(buffer);
		insert(buffer, 4);
		insert(buffer, 5);
		const JitterBuffer::Event event = pop(buffer);
		CHECK(event.seq == 3);
		CHECK(event.resumed);
	}
	SUBCASE("packets missed while idle are skipped, not concealed") {
		insert_range(buffer, 5, 3); // 3 and 4 never arrived
		const JitterBuffer::Event event = pop(buffer);
		CHECK(event.type == EventType::PACKET);
		CHECK(event.seq == 5);
		CHECK(buffer.get_stats().packets_skipped == 2);
		CHECK(buffer.get_stats().frames_concealed == 0);
	}
	SUBCASE("idle time doesn't count toward priming once the buffer is empty") {
		for (int i = 0; i < 10; i++) {
			expect_none(buffer);
		}
		insert(buffer, 3);
		expect_none(buffer); // must wait again, not play at once
	}
}

TEST_CASE("gaps longer than the concealment cap are skipped") {
	JitterBuffer buffer;
	JitterBuffer::Settings settings;
	settings.max_conceal_samples = 3 * FRAME;
	buffer.set_settings(settings);

	insert_range(buffer, 0, 11, { 3, 4, 5, 6, 7 });
	expect_packet(buffer, 0);
	expect_packet(buffer, 1);
	expect_packet(buffer, 2);
	const JitterBuffer::Event event = pop(buffer);
	REQUIRE(event.type == EventType::PACKET);
	CHECK(event.seq == 8);
	CHECK(event.resumed);
	CHECK(buffer.get_stats().packets_skipped == 5);
	CHECK(buffer.get_stats().frames_concealed == 0);

	// A gap within the cap is still concealed.
	insert_range(buffer, 11, 4, { 11, 12, 13 });
	expect_packet(buffer, 9);
	expect_packet(buffer, 10);
	expect_conceal(buffer, 14, 3);
}

TEST_CASE("settings are validated") {
	JitterBuffer buffer;
	JitterBuffer::Settings settings;
	settings.target_samples = 4800;
	settings.max_samples = 960; // below the target
	settings.max_conceal_samples = -1;
	buffer.set_settings(settings);
	CHECK(buffer.get_settings().max_samples == 4800);
	CHECK(buffer.get_settings().max_conceal_samples == 0);
}

TEST_CASE("clear forgets the stream but keeps stats") {
	JitterBuffer buffer;
	insert_range(buffer, 100, 3);
	expect_packet(buffer, 100);
	buffer.clear();
	CHECK_FALSE(buffer.is_playing());
	CHECK(buffer.get_stats().depth_samples == 0);
	// A new stream anchors anywhere without counting as a restart.
	insert_range(buffer, 0, 3);
	expect_packet(buffer, 0);
	CHECK(buffer.get_stats().resyncs == 0);
	CHECK(buffer.get_stats().packets_played == 2);
}
