#!/usr/bin/env python

import glob
import hashlib
import os
import re
import shutil
import tarfile
import urllib.request

env = SConscript("third-party/godot-cpp/SConstruct")

# -----------------------------------------------------------------------------
# Output directories
# -----------------------------------------------------------------------------

addon_src = "addon"
build_dir = "build/addons/godotopus"
bin_dir = os.path.join(build_dir, "bin")

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
# Opus DNN features (DRED, DNN-based PLC, OSCE/BWE) — opt-in, heavy
# -----------------------------------------------------------------------------
#
# libopus's deep-learning features (Deep REDundancy, DNN packet-loss
# concealment, OSCE speech enhancement + blind bandwidth extension) need a
# ~130MB set of pretrained-weight C source files that aren't checked into the
# opus submodule (same as upstream — see third-party/opus/dnn/download_model.*).
# They're only fetched/compiled when explicitly requested, since they roughly
# double the binary size and compile time:
#
#   scons dred=yes
#
# The weights are downloaded straight into the (gitignored, untouched-by-us-
# otherwise) opus submodule checkout, exactly where its own download script
# would put them, and are cached there across builds — only re-fetched if
# missing or if the pinned model hash in third-party/opus/autogen.sh changes.

dred_enabled = ARGUMENTS.get("dred", "no").lower() in ("yes", "true", "1")

opus_dnn_dir = "third-party/opus/dnn"
opus_dnn_stamp = os.path.join(opus_dnn_dir, ".opus_dnn_weights.stamp")

# Mirrors third-party/opus/lpcnet_sources.mk. The SIMD-dispatch variants
# (dnn/x86, dnn/arm) are intentionally left out, same as the celt/x86 and
# celt/arm equivalents already excluded from opus_src above.
opus_dnn_deep_plc_src = [
    "burg.c",
    "freq.c",
    "fargan.c",
    "fargan_data.c",
    "lpcnet_enc.c",
    "lpcnet_plc.c",
    "lpcnet_tables.c",
    "nnet.c",
    "nnet_default.c",
    "plc_data.c",
    "parse_lpcnet_weights.c",
    "pitchdnn.c",
    "pitchdnn_data.c",
]
opus_dnn_dred_src = [
    "dred_rdovae_enc.c",
    "dred_rdovae_enc_data.c",
    "dred_rdovae_dec.c",
    "dred_rdovae_dec_data.c",
    "dred_rdovae_stats_data.c",
    "dred_encoder.c",
    "dred_coding.c",
    "dred_decoder.c",
]
opus_dnn_osce_src = [
    "osce.c",
    "osce_features.c",
    "nndsp.c",
    "lace_data.c",
    "nolace_data.c",
    "bbwenet_data.c",
]

opus_dnn_filenames = opus_dnn_deep_plc_src + opus_dnn_dred_src + opus_dnn_osce_src
opus_dnn_src = [os.path.join(opus_dnn_dir, f) for f in opus_dnn_filenames]


def _pinned_opus_model_hash():
    autogen_path = "third-party/opus/autogen.sh"
    with open(autogen_path, "r") as f:
        content = f.read()
    match = re.search(r'download_model\.sh\s+"([0-9a-f]+)"', content)
    if not match:
        raise SCons.Errors.StopError(
            "godotopus: could not find the pinned Opus DNN model hash in " + autogen_path
        )
    return match.group(1)


def _download_opus_dnn_weights(target, source, env):
    model_hash = _pinned_opus_model_hash()
    archive_name = "opus_data-%s.tar.gz" % model_hash
    archive_path = os.path.join(opus_dnn_dir, archive_name)
    url = "https://media.xiph.org/opus/models/%s" % archive_name

    if not os.path.isfile(archive_path):
        print("godotopus: downloading Opus DNN model weights (~130MB) from %s ..." % url)
        urllib.request.urlretrieve(url, archive_path)

    print("godotopus: verifying Opus DNN model weights checksum...")
    sha256 = hashlib.sha256()
    with open(archive_path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            sha256.update(chunk)
    if sha256.hexdigest() != model_hash:
        os.remove(archive_path)
        raise SCons.Errors.StopError(
            "godotopus: Opus DNN model download failed checksum verification, deleted. Try again."
        )

    print("godotopus: extracting Opus DNN model source files...")
    with tarfile.open(archive_path, "r:gz") as tar:
        members = [
            m for m in tar.getmembers()
            if m.name.startswith("dnn/") and m.name.endswith((".c", ".h"))
        ]
        tar.extractall("third-party/opus", members=members)


if dred_enabled:
    # Declaring every extracted .c we compile (not just the stamp) as a
    # target lets SCons treat them as derived nodes it knows how to produce,
    # instead of erroring out because they don't exist on disk yet.
    dnn_weights = env.Command(
        [opus_dnn_stamp] + opus_dnn_src,
        "third-party/opus/autogen.sh",  # pins the model hash; re-fetches if it changes
        _download_opus_dnn_weights,
    )

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
opus_env.Append(
    CPPPATH=[
        "third-party/opus/include",
        "third-party/opus/celt",
        "third-party/opus/silk",
        "third-party/opus/silk/float",
    ]
)

if dred_enabled:
    opus_env.Append(
        CPPDEFINES=[
            "ENABLE_DEEP_PLC",
            "ENABLE_DRED",
            "ENABLE_OSCE",
            "ENABLE_OSCE_BWE",
        ]
    )
    opus_env.Append(CPPPATH=[opus_dnn_dir])

opus_objs = [opus_env.SharedObject(f) for f in opus_src]

if dred_enabled:
    opus_dnn_objs = [opus_env.SharedObject(f) for f in opus_dnn_src]
    env.Depends(opus_dnn_objs, dnn_weights)
    opus_objs += opus_dnn_objs

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

speex_obj = speex_env.SharedObject("third-party/speexdsp/libspeexdsp/resample.c")
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
# Copy addon folder
# -----------------------------------------------------------------------------


def copy_addon(target, source, env):
    # Clean out anything previously mirrored from addon_src before re-copying
    # (but leave bin/ alone, that's the compiled library, not part of
    # addon_src). Otherwise files deleted from addon_src — like the old
    # vad.gd — silently linger forever in build_dir instead of going away.
    if os.path.isdir(build_dir):
        for entry in os.listdir(build_dir):
            if entry == "bin":
                continue
            path = os.path.join(build_dir, entry)
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)
    shutil.copytree(addon_src, build_dir, dirs_exist_ok=True)
    print(f"Copied {addon_src} -> {build_dir}")


addon = env.Command(
    build_dir,
    addon_src,
    copy_addon,
)

# Ensure addon is copied after the library is built.
env.Depends(addon, library)

# SCons only reliably detects added/removed files in addon_src, not in-place
# content edits to files that already existed (it does not hash a Dir source's
# contents file-by-file). Rather than rely on that, always re-run the copy on
# every invocation (cheap file I/O) so plain `scons` always mirrors whatever
# is currently in addon/, never a stale copy from a previous build.
env.AlwaysBuild(addon)

Default(library, addon)
