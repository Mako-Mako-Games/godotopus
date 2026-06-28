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

# Exclude demo/test files that define their own main()
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

# Your sources
sources = glob.glob("src/*.cpp") + opus_src

lib_name = "libgodotopus"
env.Append(CPPPATH=["src/"])

# Output DLLs/SOs into addons/godot-opus/bin/
bin_dir = "addons/godot-opus/bin"

library = env.SharedLibrary(
    f"{bin_dir}/{lib_name}{env['suffix']}{env['SHLIBSUFFIX']}",
    source=sources,
)

# Copy the .gdextension file into addons/godot-opus/ after build
addon_dir = "addons/godot-opus"
gdextension_src = "godot-opus.gdextension"

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