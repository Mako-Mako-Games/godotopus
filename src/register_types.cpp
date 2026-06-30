#include "register_types.hpp"
#include "godot_opus_codec.hpp"
#include "godot_opus_resampler.hpp"
#include "audio_input_provider.hpp"
#include "audio_input_multiplexer.hpp"
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

static AudioInputMultiplexer *audio_input_multiplexer_singleton = nullptr;
void initialize_godot_opus(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    ClassDB::register_class<GodotOpusEncoder>();
    ClassDB::register_class<GodotOpusDecoder>();
    ClassDB::register_class<GodotOpusResampler>();
    ClassDB::register_class<AudioInputProvider>();
    ClassDB::register_class<AudioInputMultiplexer>();

    audio_input_multiplexer_singleton = memnew(AudioInputMultiplexer);
    Engine::get_singleton()->register_singleton("AudioInputMultiplexer", audio_input_multiplexer_singleton);
}

void uninitialize_godot_opus(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    Engine::get_singleton()->unregister_singleton("AudioInputMultiplexer");
    memdelete(audio_input_multiplexer_singleton);
    audio_input_multiplexer_singleton = nullptr;
}
extern "C" {
GDExtensionBool GDE_EXPORT godot_opus_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization)
{
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_godot_opus);
    init_obj.register_terminator(uninitialize_godot_opus);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}