#include "godot_opus_codec.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── GodotOpusEncoder ──────────────────────────────────────────────────────────────

void GodotOpusEncoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels"), &GodotOpusEncoder::initialize);
    ClassDB::bind_method(D_METHOD("encode", "pcm"), &GodotOpusEncoder::encode);
    ClassDB::bind_method(D_METHOD("set_bitrate", "bitrate"), &GodotOpusEncoder::set_bitrate);
    ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusEncoder::set_frame_size);
    ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusEncoder::get_frame_size);
}

void GodotOpusEncoder::initialize(int p_sample_rate, int p_channels) {
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
    sample_rate = p_sample_rate;
    channels = p_channels;

    int error;
    encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        UtilityFunctions::printerr("GodotOpusEncoder: failed to create encoder: ", opus_strerror(error));
        encoder = nullptr;
        return;
    }

    // Good defaults for voice chat
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(encoder, OPUS_SET_DTX(1)); // silence = tiny packets
}

PackedByteArray GodotOpusEncoder::encode(const PackedFloat32Array &p_pcm) {
    PackedByteArray result;
    if (!encoder) {
        UtilityFunctions::printerr("GodotOpusEncoder: not initialized");
        return result;
    }
    if (p_pcm.size() != frame_size * channels) {
        UtilityFunctions::printerr(
            "GodotOpusEncoder: expected ", frame_size * channels,
            " samples, got ", p_pcm.size());
        return result;
    }

    // Max opus packet size
    const int max_packet = 4000;
    result.resize(max_packet);

    int bytes_written = opus_encode_float(
        encoder,
        p_pcm.ptr(),
        frame_size,
        result.ptrw(),
        max_packet
    );

    if (bytes_written < 0) {
        UtilityFunctions::printerr("GodotOpusEncoder: encode failed: ", opus_strerror(bytes_written));
        result.clear();
        return result;
    }

    result.resize(bytes_written);
    return result;
}

void GodotOpusEncoder::set_bitrate(int p_bitrate) {
    if (encoder) {
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(p_bitrate));
    }
}

void GodotOpusEncoder::set_frame_size(int p_frame_size) {
    frame_size = p_frame_size;
}

int GodotOpusEncoder::get_frame_size() const {
    return frame_size;
}

GodotOpusEncoder::~GodotOpusEncoder() {
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
}

// ── GodotOpusDecoder ──────────────────────────────────────────────────────────────

void GodotOpusDecoder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize", "sample_rate", "channels"), &GodotOpusDecoder::initialize);
    ClassDB::bind_method(D_METHOD("decode", "packet"), &GodotOpusDecoder::decode);
    ClassDB::bind_method(D_METHOD("set_frame_size", "frame_size"), &GodotOpusDecoder::set_frame_size);
    ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpusDecoder::get_frame_size);
}

void GodotOpusDecoder::initialize(int p_sample_rate, int p_channels) {
    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }
    sample_rate = p_sample_rate;
    channels = p_channels;

    int error;
    decoder = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK) {
        UtilityFunctions::printerr("GodotOpusDecoder: failed to create decoder: ", opus_strerror(error));
        decoder = nullptr;
    }
}

PackedFloat32Array GodotOpusDecoder::decode(const PackedByteArray &p_packet) {
    PackedFloat32Array result;
    if (!decoder) {
        UtilityFunctions::printerr("GodotOpusDecoder: not initialized");
        return result;
    }

    result.resize(frame_size * channels);

    int samples_decoded = opus_decode_float(
        decoder,
        p_packet.ptr(),
        p_packet.size(),
        result.ptrw(),
        frame_size,
        0 // no FEC on decode side
    );

    if (samples_decoded < 0) {
        UtilityFunctions::printerr("GodotOpusDecoder: decode failed: ", opus_strerror(samples_decoded));
        result.clear();
        return result;
    }

    result.resize(samples_decoded * channels);
    return result;
}

void GodotOpusDecoder::set_frame_size(int p_frame_size) {
    frame_size = p_frame_size;
}

int GodotOpusDecoder::get_frame_size() const {
    return frame_size;
}

GodotOpusDecoder::~GodotOpusDecoder() {
    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }
}