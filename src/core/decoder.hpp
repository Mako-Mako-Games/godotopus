#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <opus.h>

namespace godotopus {

// What an Opus packet says about itself. Opus packets are self-describing,
// so a receiver needs no settings from the sender to decode them.
struct PacketInfo {
	int samples = 0;  // per channel, at the sample rate passed to inspect_packet
	int channels = 0; // channels coded in the packet (a stereo encoder may code mono)
	int frames = 0;	  // Opus frames inside the packet
};

// Fills r_info from the packet's header. Returns OPUS_OK or an error code
// if the packet is malformed.
int inspect_packet(const uint8_t *p_data, size_t p_size, int p_sample_rate, PacketInfo &r_info);

// Owns one libopus decoder plus the optional DRED decoder state.
// Output is interleaved float PCM at the decoder's sample rate. Methods
// returning int give samples decoded per channel (>= 0) or a negative Opus
// error code, unless documented otherwise.
class Decoder {
public:
	Decoder() = default;
	~Decoder();
	Decoder(const Decoder &) = delete;
	Decoder &operator=(const Decoder &) = delete;

	int init(int p_sample_rate, int p_channels);
	bool is_initialized() const { return decoder != nullptr; }

	// Decodes a packet of any duration Opus supports (up to 120 ms). The
	// output is sized from the packet itself, not from local settings.
	int decode(const uint8_t *p_data, size_t p_size, std::vector<float> &r_pcm);

	// Packet loss concealment for p_frame_samples of missing audio.
	int conceal(int p_frame_samples, std::vector<float> &r_pcm);

	// Recovers the frame lost just before p_data from its in-band FEC data
	// (falls back to concealment if there is none). Does not decode p_data's
	// own audio; call decode() for that afterwards.
	int decode_fec(const uint8_t *p_data, size_t p_size, int p_frame_samples, std::vector<float> &r_pcm);

	// Parses the DRED payload of p_data. Returns how many samples of history
	// before the packet can be recovered with decode_dred(), or 0 if the
	// packet has none or this build lacks DRED support.
	int parse_dred(const uint8_t *p_data, size_t p_size);
	// Decodes p_frame_samples starting p_offset_samples before the packet last
	// given to parse_dred(). p_offset_samples must be <= its return value.
	int decode_dred(int p_offset_samples, int p_frame_samples, std::vector<float> &r_pcm);

	// 0..10. Values >= 5 enable deep PLC and >= 6/7 OSCE enhancement on DNN builds.
	int set_complexity(int p_complexity);
	int get_complexity() const;

	// OSCE blind bandwidth extension. OPUS_UNIMPLEMENTED on builds without it.
	int set_bandwidth_extension(bool p_enabled);
	bool get_bandwidth_extension() const;
	bool supports_bandwidth_extension() const;

	int reset();

	int get_sample_rate() const { return sample_rate; }
	int get_channels() const { return channels; }
	// Duration (samples per channel) of the last packet decoded, or 0.
	int get_last_packet_samples() const { return last_packet_samples; }

private:
	void destroy();
	bool ensure_dred();
	int finish(int p_result, std::vector<float> &r_pcm);

	OpusDecoder *decoder = nullptr;
	OpusDREDDecoder *dred_decoder = nullptr;
	OpusDRED *dred = nullptr;
	bool dred_unavailable = false;
	int dred_samples = 0;
	int sample_rate = 0;
	int channels = 0;
	int last_packet_samples = 0;
};

} // namespace godotopus
