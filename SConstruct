#!/usr/bin/env python

import glob
import os
import shutil

env = SConscript("third-party/godot-cpp/SConstruct")

# -----------------------------------------------------------------------------
# Output directories
# -----------------------------------------------------------------------------

build_dir = "build/addons/godotopus"
bin_dir = os.path.join(build_dir, "bin")

gdextension_src = "addon/godotopus.gdextension"
scripts_src = "addon/scripts"

# -----------------------------------------------------------------------------
# Generate speexdsp_config_types.h (normally produced by autoconf)
# -----------------------------------------------------------------------------

speex_config_types_path = "third-party/speexdsp/include/speex/speexdsp_config_types.h"


def generate_speex_config_types(target, source, env):
    content = (
        "#ifndef SPEEXDSP_CONFIG_TYPES_H\n"
        "#define SPEEXDSP_CONFIG_TYPES_H\n\n"
        "#include <stdint.h>\n\n"
        "typedef int16_t spx_int16_t;\n"
        "typedef uint16_t spx_uint16_t;\n"
        "typedef int32_t spx_int32_t;\n"
        "typedef uint32_t spx_uint32_t;\n\n"
        "#endif /* SPEEXDSP_CONFIG_TYPES_H */\n"
    )
    with open(target[0].abspath, "w") as f:
        f.write(content)
    print(f"Generated {speex_config_types_path}")


speex_config_types = env.Command(
    speex_config_types_path,
    "third-party/speexdsp/include/speex/speexdsp_config_types.h.in",
    generate_speex_config_types,
)

# -----------------------------------------------------------------------------
# Opus sources
# -----------------------------------------------------------------------------

opus_src = (
    glob.glob("third-party/opus/src/*.c")
    + glob.glob("third-party/opus/celt/*.c")
    + glob.glob("third-party/opus/silk/*.c")
    + glob.glob("third-party/opus/silk/float/*.c")
)

exclude = [
    "opus_demo.c",
    "opus_compare.c",
    "qext_compare.c",
    "repacketizer_demo.c",
    "opus_projection_demo.c",
    "opus_custom_demo.c",
]

opus_src = [
    f for f in opus_src if not any(f.replace("\\", "/").endswith(x) for x in exclude)
]

# -----------------------------------------------------------------------------
# Opus configuration (isolated env — do not let these defines leak into
# Speex or plugin sources)
# -----------------------------------------------------------------------------

opus_env = env.Clone()
opus_env.Append(
    CPPDEFINES=[
        "OPUS_BUILD",
        "USE_ALLOCA",
        "HAVE_LRINT",
        "HAVE_LRINTF",
        "FLOAT_APPROX",
        "OPUS_HAVE_RTCD",
    ]
)
if opus_env["platform"] in ("macos", "linux"):
    opus_env.Append(CCFLAGS=["-fPIC"])
opus_env.Append(
    CPPPATH=[
        "third-party/opus/include",
        "third-party/opus/celt",
        "third-party/opus/silk",
        "third-party/opus/silk/float",
    ]
)

opus_objs = [opus_env.Object(f) for f in opus_src]

# -----------------------------------------------------------------------------
# SpeexDSP configuration (isolated env — do not let these defines leak into
# Opus or plugin sources)
# -----------------------------------------------------------------------------

speex_env = env.Clone()
speex_env.Append(
    CPPDEFINES=[
        "OUTSIDE_SPEEX",
        "RANDOM_PREFIX=godotopus",
        "FLOATING_POINT",
        "EXPORT=",
    ]
)
speex_env.Append(
    CPPPATH=[
        "third-party/speexdsp/include",
        "third-party/speexdsp/libspeexdsp",
        "third-party/speexdsp/include/speex",
    ]
)
if speex_env["platform"] in ("macos", "linux"):
    speex_env.Append(CCFLAGS=["-fPIC"])

speex_obj = speex_env.Object("third-party/speexdsp/libspeexdsp/resample.c")
env.Depends(speex_obj, speex_config_types)

# -----------------------------------------------------------------------------
# Plugin sources — only needs include paths, none of the Opus/Speex macros
# -----------------------------------------------------------------------------

env.Append(
    CPPDEFINES=[
        "OUTSIDE_SPEEX",
        "RANDOM_PREFIX=godotopus",
        "FLOATING_POINT",
        "EXPORT=",
    ]
)
env.Append(
    CPPPATH=[
        "src",
        "third-party/opus/include",
        "third-party/speexdsp/include",
    ]
)

sources = glob.glob("src/*.cpp") + opus_objs + [speex_obj]

lib_name = "libgodotopus"

library = env.SharedLibrary(
    os.path.join(bin_dir, f"{lib_name}{env['suffix']}{env['SHLIBSUFFIX']}"),
    source=sources,
)
env.Depends(library, speex_config_types)

# -----------------------------------------------------------------------------
# Copy addon files
# -----------------------------------------------------------------------------


def copy_gdextension(target, source, env):
    os.makedirs(build_dir, exist_ok=True)
    shutil.copy2(gdextension_src, target[0].abspath)
    print(f"Copied {gdextension_src}")


gdextension = env.Command(
    os.path.join(build_dir, "godotopus.gdextension"),
    gdextension_src,
    copy_gdextension,
)


def copy_scripts(target, source, env):
    dst = os.path.join(build_dir, "scripts")
    shutil.copytree(scripts_src, dst, dirs_exist_ok=True)
    print(f"Copied {scripts_src} -> {dst}")


scripts = env.Command(
    os.path.join(build_dir, "scripts"),
    scripts_src,
    copy_scripts,
)

# Ensure addon files are copied after the library is built.
env.Depends(gdextension, library)
env.Depends(scripts, library)

Default(library, gdextension, scripts)
