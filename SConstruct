#!/usr/bin/env python
#
# Builds the Godotopus GDExtension into addons/godotopus/bin/.
#
#   scons                      light build (no DNN features)
#   scons dnn=yes              heavy build: DRED, deep PLC, OSCE/BWE
#   scons install              also copy the addon into tests/godot and demo
#
# All generated files go under build/; submodules are never modified.

import glob
import os
import sys

sys.path.insert(0, os.path.abspath("tools/scons"))

import install  # noqa: E402
import opus  # noqa: E402
import speexdsp  # noqa: E402
import version  # noqa: E402

env = SConscript("third-party/godot-cpp/SConstruct")

dnn_enabled = ARGUMENTS.get("dnn", "no").lower() in ("yes", "true", "1")

addon_dir = "addons/godotopus"
gen_dir = "build/gen"

opus_objs, opus_includes = opus.build(env, gen_dir, dnn_enabled)
speex_objs, speex_includes, speex_config = speexdsp.build(env, gen_dir)
version_header, version_include = version.build(env, gen_dir)

# Plugin sources. The Speex defines are needed here too: RANDOM_PREFIX is
# applied by speex_resampler.h's macros, so callers must see the same prefix.
env.Append(CPPDEFINES=speexdsp.SPEEX_DEFINES)
env.Append(CPPPATH=["src", version_include] + opus_includes + speex_includes)

plugin_sources = sorted(glob.glob("src/godot/*.cpp"))
plugin_objs = [env.SharedObject(f) for f in plugin_sources]
env.Depends(plugin_objs, [speex_config, version_header])

library = env.SharedLibrary(
    os.path.join(addon_dir, "bin", "libgodotopus{}{}".format(env["suffix"], env["SHLIBSUFFIX"])),
    source=plugin_objs + opus_objs + speex_objs,
)

install.build(env, library)

Default(library)
