#pragma once
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

    class AudioInputProvider : public Object {
        GDCLASS(AudioInputProvider, Object)

        static constexpr int RING_CAPACITY = 65536; // must be power of 2

        Vector2 ring[RING_CAPACITY];
        int write_pos = 0;
        int read_pos  = 0;

    protected:
        static void _bind_methods();

    public:
        AudioInputProvider();
        ~AudioInputProvider();

        int get_frames_available();
        PackedVector2Array get_frames(int p_count);

        void push(const Vector2 *p_frames, int p_count); // called by mux only
    };

} // namespace godot