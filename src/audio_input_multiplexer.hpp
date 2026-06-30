#pragma once
#include <godot_cpp/classes/object.hpp>
#include <vector>

namespace godot {

    class AudioInputProvider;

    class AudioInputMultiplexer : public Object {
        GDCLASS(AudioInputMultiplexer, Object)

        static AudioInputMultiplexer *singleton;
        std::vector<AudioInputProvider *> providers;

    protected:
        static void _bind_methods();

    public:
        static AudioInputMultiplexer *get_singleton();

        AudioInputMultiplexer();
        ~AudioInputMultiplexer();

        void register_provider(AudioInputProvider *p_provider);
        void unregister_provider(AudioInputProvider *p_provider);

        // Drains all pending frames from AudioServer into every provider's buffer.
        // No-ops immediately if the server buffer is empty.
        void drain();
    };

} // namespace godot