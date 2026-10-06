#pragma once

// Sample-rate and frame-size rules shared by the encoder, decoder and the
// receive pipeline. Opus only accepts a fixed set of each.

namespace godotopus {

// Every stream is decoded at Opus's native rate. The bitstream doesn't
// depend on the sender's rate, so receivers never need to know it.
constexpr int DECODE_SAMPLE_RATE = 48000;

// Longest audio a single Opus packet can carry.
constexpr int MAX_PACKET_DURATION_MS = 120;

// Upper bound for one encoded packet; the libopus docs recommend 4000 bytes.
constexpr int MAX_PACKET_BYTES = 4000;

// The rates Opus can encode and decode at.
bool is_valid_sample_rate(int p_sample_rate);

// The frame durations this addon exposes: 5, 10, 20, 40 or 60 ms.
bool is_valid_frame_duration_ms(int p_duration_ms);

// Samples per channel in one frame of p_duration_ms at p_sample_rate.
int frame_samples(int p_sample_rate, int p_duration_ms);

// True if p_samples (per channel) is a frame size Opus accepts at
// p_sample_rate: 2.5, 5, 10, 20, 40, 60, 80, 100 or 120 ms.
bool is_valid_frame_samples(int p_sample_rate, int p_samples);

} // namespace godotopus
