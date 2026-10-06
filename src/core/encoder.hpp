#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <opus.h>

namespace godotopus {

// Opus marks DTX ("nothing to say") frames by returning 2 bytes or less from
// opus_encode. Those never need to be transmitted.
inline bool is_dtx_packet(size_t p_bytes) {
	return p_bytes <= 2;
}

// Owns one libopus encoder configured for real-time voice.
// Methods returning int give OPUS_OK or a negative Opus error code
// (see opus_strerror) unless documented otherwise.
class Encoder {
public:
	Encoder() = default;
	~Encoder();
	Encoder(const Encoder &) = delete;
	Encoder &operator=(const Encoder &) = delete;

	// (Re)creates the encoder and applies voice defaults: 24 kb/s VBR,
	// complexity 10, voice signal hint, in-band FEC, 10% expected loss, DTX on.
	int init(int p_sample_rate, int p_channels);
	bool is_initialized() const { return encoder != nullptr; }

	// Encodes one frame of interleaved PCM into r_packet. p_frame_samples is
	// per channel and must satisfy is_valid_frame_samples(). Returns the
	// packet size in bytes (see is_dtx_packet) or a negative error code.
	int encode(const float *p_pcm, int p_frame_samples, std::vector<uint8_t> &r_packet);

	int set_bitrate(int p_bits_per_second);
	int get_bitrate() const;
	int set_complexity(int p_complexity); // 0..10
	int set_vbr(bool p_enabled, bool p_constrained);
	int set_signal_voice(bool p_enabled);
	int set_inband_fec(bool p_enabled);
	int set_expected_packet_loss(int p_percent); // 0..100
	int set_dtx(bool p_enabled);
	// True if the last encoded frame was classified as silence.
	bool is_in_dtx() const;

	// Deep REDundancy history to embed, rounded down to 10 ms steps. Returns
	// OPUS_UNIMPLEMENTED on builds without DNN support.
	int set_dred_duration_ms(int p_duration_ms);
	int get_dred_duration_ms() const;
	bool supports_dred() const;

	int reset();

	int get_sample_rate() const { return sample_rate; }
	int get_channels() const { return channels; }

private:
	void destroy();

	OpusEncoder *encoder = nullptr;
	int sample_rate = 0;
	int channels = 0;
};

} // namespace godotopus
