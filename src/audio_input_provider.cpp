#include "audio_input_provider.hpp"
#include "audio_input_multiplexer.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cstring>

namespace godot {

    void AudioInputProvider::_bind_methods() {
        ClassDB::bind_method(D_METHOD("get_frames_available"), &AudioInputProvider::get_frames_available);
        ClassDB::bind_method(D_METHOD("get_frames", "count"),  &AudioInputProvider::get_frames);
    }

    AudioInputProvider::AudioInputProvider() {
        memset(ring, 0, sizeof(ring));
        AudioInputMultiplexer *mux = AudioInputMultiplexer::get_singleton();
        ERR_FAIL_NULL_MSG(mux, "AudioInputMultiplexer singleton not found.");
        mux->register_provider(this);
    }

    AudioInputProvider::~AudioInputProvider() {
        AudioInputMultiplexer *mux = AudioInputMultiplexer::get_singleton();
        if (mux) mux->unregister_provider(this);
    }

    int AudioInputProvider::get_frames_available() {
        AudioInputMultiplexer *mux = AudioInputMultiplexer::get_singleton();
        if (mux) mux->drain();
        return write_pos - read_pos;
    }

    PackedVector2Array AudioInputProvider::get_frames(int p_count) {
        AudioInputMultiplexer *mux = AudioInputMultiplexer::get_singleton();
        if (mux) mux->drain();

        PackedVector2Array out;
        out.resize(p_count);
        Vector2 *dst = out.ptrw();

        int available = write_pos - read_pos;
        int to_copy   = std::min(p_count, available);
        int silence   = p_count - to_copy;

        for (int i = 0; i < to_copy; ++i) {
            dst[i] = ring[(read_pos + i) & (RING_CAPACITY - 1)];
        }
        read_pos += to_copy;

        if (silence > 0) {
            memset(dst + to_copy, 0, silence * sizeof(Vector2));
        }

        return out;
    }

    void AudioInputProvider::push(const Vector2 *p_frames, int p_count) {
        for (int i = 0; i < p_count; ++i) {
            ring[write_pos & (RING_CAPACITY - 1)] = p_frames[i];
            write_pos++;
            if (write_pos - read_pos > RING_CAPACITY) {
                read_pos++; // drop oldest on overflow
            }
        }
    }

} // namespace godot