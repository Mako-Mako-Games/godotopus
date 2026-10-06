#!/usr/bin/env python
#
# Builds the Godotopus GDExtension into addons/godotopus/bin/.
#
#   scons                      light build (no DNN features)
#   scons dnn=yes              heavy build: DRED, deep PLC, OSCE/BWE
#   scons install              also copy the addon into tests/godot and demo
#   scons tests                build and run the native C++ test suite
#   scons tests sanitize=yes   same, with AddressSanitizer + UBSan (GCC/Clang)
#
# All generated files go under build/; submodules are never modified.

import glob
import os
import sys

sys.path.insert(0, os.path.abspath("tools/scons"))

import install  # noqa: E402
import native_tests  # noqa: E402
import opus  # noqa: E402
import speexdsp  # noqa: E402
import version  # noqa: E402

env = SConscript("third-party/godot-cpp/SConstruct")


def _flag(name):
    return ARGUMENTS.get(name, "no").lower() in ("yes", "true", "1")


dnn_enabled = _flag("dnn")

addon_dir = "addons/godotopus"
gen_dir = "build/gen"

opus_objs, opus_includes = opus.build(env, gen_dir, dnn_enabled)
speex_objs, speex_includes, speex_config = speexdsp.build(env, gen_dir)
version_header, version_include = version.build(env, gen_dir)

# Plugin sources. The Speex defines are needed here too: RANDOM_PREFIX is
# applied by speex_resampler.h's macros, so callers must see the same prefix.
env.Append(CPPDEFINES=speexdsp.SPEEX_DEFINES)
env.Append(CPPPATH=["src", version_include] + opus_includes + speex_includes)

# src/core is plain C++ (no godot-cpp), shared by the extension and the
# native tests; src/godot holds the GDExtension classes.
core_sources = sorted(glob.glob("src/core/*.cpp"))
plugin_sources = core_sources + sorted(glob.glob("src/godot/*.cpp"))
plugin_objs = [env.SharedObject(f) for f in plugin_sources]
env.Depends(plugin_objs, [speex_config, version_header])

library = env.SharedLibrary(
    os.path.join(addon_dir, "bin", "libgodotopus{}{}".format(env["suffix"], env["SHLIBSUFFIX"])),
    source=plugin_objs + opus_objs + speex_objs,
)

install.build(env, library)

tests = native_tests.build(env, core_sources, opus_objs + speex_objs, opus_includes + speex_includes, _flag("sanitize"))
env.Depends(tests, speex_config)

Default(library)
