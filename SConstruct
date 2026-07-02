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
# Opus configuration
# -----------------------------------------------------------------------------

env.Append(
    CPPDEFINES=[
        "OPUS_BUILD",
        "USE_ALLOCA",
        "HAVE_LRINT",
        "HAVE_LRINTF",
        "FLOAT_APPROX",
        "OPUS_HAVE_RTCD",
    ]
)

env.Append(
    CPPPATH=[
        "third-party/opus/include",
        "third-party/opus/celt",
        "third-party/opus/silk",
        "third-party/opus/silk/float",
    ]
)

# -----------------------------------------------------------------------------
# SpeexDSP configuration
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
        "third-party/speexdsp/include",
    ]
)

speex_env = env.Clone()

speex_env.Append(
    CPPPATH=[
        "third-party/speexdsp/libspeexdsp",
        "third-party/speexdsp/include/speex",
    ]
)

speex_obj = speex_env.Object("third-party/speexdsp/libspeexdsp/resample.c")

# -----------------------------------------------------------------------------
# Plugin sources
# -----------------------------------------------------------------------------

env.Append(CPPPATH=["src"])

sources = glob.glob("src/*.cpp") + opus_src + [speex_obj]

lib_name = "libgodotopus"

library = env.SharedLibrary(
    os.path.join(bin_dir, f"{lib_name}{env['suffix']}{env['SHLIBSUFFIX']}"),
    source=sources,
)

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
