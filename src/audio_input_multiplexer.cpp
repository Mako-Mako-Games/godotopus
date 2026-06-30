#include "audio_input_multiplexer.hpp"
#include "audio_input_provider.hpp"
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>

namespace godot {

AudioInputMultiplexer *AudioInputMultiplexer::singleton = nullptr;

AudioInputMultiplexer *AudioInputMultiplexer::get_singleton() { return singleton; }

void AudioInputMultiplexer::_bind_methods() {}

AudioInputMultiplexer::AudioInputMultiplexer() {
    ERR_FAIL_COND(singleton != nullptr);
    singleton = this;
}

AudioInputMultiplexer::~AudioInputMultiplexer() {
    ERR_FAIL_COND(singleton != this);
    singleton = nullptr;
}

void AudioInputMultiplexer::register_provider(AudioInputProvider *p_provider) {
    providers.push_back(p_provider);
    if (providers.size() == 1) {
        AudioServer::get_singleton()->set_input_device_active(true);
    }
}

void AudioInputMultiplexer::unregister_provider(AudioInputProvider *p_provider) {
    providers.erase(std::remove(providers.begin(), providers.end(), p_provider), providers.end());
    if (providers.empty()) {
        AudioServer::get_singleton()->set_input_device_active(false);
    }
}

void AudioInputMultiplexer::drain() {
    if (providers.empty()) return;

    AudioServer *as = AudioServer::get_singleton();
    int available = as->get_input_frames_available();
    if (available <= 0) return;

    PackedVector2Array frames = as->get_input_frames(available);
    const Vector2 *ptr = frames.ptr();
    int count = frames.size();

    for (AudioInputProvider *provider : providers) {
        provider->push(ptr, count);
    }
}

} // namespace godot