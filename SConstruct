#!/usr/bin/env python
import os
import glob
import shutil

env = SConscript("third-party/godot-cpp/SConstruct")

# Opus sources
opus_src = (
    glob.glob("third-party/opus/src/*.c") +
    glob.glob("third-party/opus/celt/*.c") +
    glob.glob("third-party/opus/silk/*.c") +
    glob.glob("third-party/opus/silk/float/*.c")
)

exclude = [
    "opus_demo.c",
    "opus_compare.c",
    "qext_compare.c",
    "repacketizer_demo.c",
    "opus_projection_demo.c",
    "opus_custom_demo.c",
]
opus_src = [f for f in opus_src if not any(f.replace("\\", "/").endswith(x) for x in exclude)]

# Opus build config
env.Append(CPPDEFINES=[
    "OPUS_BUILD",
    "USE_ALLOCA",
    "HAVE_LRINT",
    "HAVE_LRINTF",
    "FLOAT_APPROX",
    "OPUS_HAVE_RTCD",
])
env.Append(CPPPATH=[
    "third-party/opus/include",
    "third-party/opus/celt",
    "third-party/opus/silk",
    "third-party/opus/silk/float",
])

# Speex defines on the global env so src/*.cpp see RANDOM_PREFIX
env.Append(CPPDEFINES=[
    "OUTSIDE_SPEEX",
    "RANDOM_PREFIX=godotopus",
    "FLOATING_POINT",
    "EXPORT=",
])
env.Append(CPPPATH=[
    "third-party/speexdsp/include",
])

# Clone AFTER all defines are appended, then add the internal path only for resample.c
speex_env = env.Clone()
speex_env.Append(CPPPATH=[
    "third-party/speexdsp/libspeexdsp",
    "third-party/speexdsp/include/speex",
])

speex_obj = speex_env.Object(
    "third-party/speexdsp/libspeexdsp/resample.c"
)

env.Append(CPPPATH=["src/"])

sources = glob.glob("src/*.cpp") + opus_src + [speex_obj]

lib_name = "libgodotopus"
bin_dir = "addons/godot_opus/bin"
library = env.SharedLibrary(
    f"{bin_dir}/{lib_name}{env['suffix']}{env['SHLIBSUFFIX']}",
    source=sources,
)

addon_dir = "addons/godot_opus"
gdextension_src = "godot_opus.gdextension"

def copy_gdextension(target, source, env):
    os.makedirs(addon_dir, exist_ok=True)
    dst = os.path.join(addon_dir, os.path.basename(gdextension_src))
    if os.path.exists(gdextension_src):
        shutil.copy2(gdextension_src, dst)
        print(f"Copied {gdextension_src} -> {dst}")
    else:
        print(f"Warning: {gdextension_src} not found, skipping copy.")

copy_action = env.Command(
    target=f"{addon_dir}/{os.path.basename(gdextension_src)}",
    source=gdextension_src,
    action=copy_gdextension,
)
env.Depends(copy_action, library)
Default(library, copy_action)
