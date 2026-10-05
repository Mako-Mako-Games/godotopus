"""libopus build, optionally with the DNN features (dnn=yes)."""

import glob
import os

import opus_dnn

OPUS_ROOT = "third-party/opus"

# Demo/test programs that live next to the library sources.
_EXCLUDE = {
    "opus_demo.c",
    "opus_compare.c",
    "qext_compare.c",
    "repacketizer_demo.c",
    "opus_projection_demo.c",
    "opus_custom_demo.c",
}

_DNN_DEFINES = ["ENABLE_DEEP_PLC", "ENABLE_DRED", "ENABLE_OSCE", "ENABLE_OSCE_BWE"]


def _base_sources():
    sources = []
    for pattern in ("src/*.c", "celt/*.c", "silk/*.c", "silk/float/*.c"):
        sources += glob.glob(os.path.join(OPUS_ROOT, pattern))
    # The SIMD variants (celt/x86, silk/x86, celt/arm, ...) are not built, so
    # libopus runs its portable C paths everywhere.
    return sorted(f for f in sources if os.path.basename(f) not in _EXCLUDE)


def _parse_make_var_lists(mk_path):
    # Reads the `VAR = a \ b \ c` blocks of an Automake fragment, so a future
    # opus bump can't silently desync our DNN file lists from upstream's.
    with open(mk_path, "r") as f:
        content = f.read().replace("\\\n", " ")
    variables = {}
    for line in content.splitlines():
        name, sep, value = line.partition("=")
        if sep:
            variables[name.strip()] = value.split()
    return variables


def _dnn_sources():
    lists = _parse_make_var_lists(os.path.join(OPUS_ROOT, "lpcnet_sources.mk"))
    return lists["DEEP_PLC_SOURCES"] + lists["DRED_SOURCES"] + lists["OSCE_SOURCES"]


def build(env, gen_dir, dnn):
    """Returns (objects, public_include_dirs)."""
    public_includes = [os.path.join(OPUS_ROOT, "include")]

    opus_env = env.Clone()
    opus_env.Append(
        CPPDEFINES=[
            "OPUS_BUILD",
            "USE_ALLOCA",
            "HAVE_LRINT",
            "HAVE_LRINTF",
            "FLOAT_APPROX",
            # No arch-specific sources are compiled, so with no OPUS_X86_* /
            # OPUS_ARM_* flags this only selects the C fallbacks at runtime.
            "OPUS_HAVE_RTCD",
        ]
    )
    opus_env.Append(
        CPPPATH=public_includes
        + [os.path.join(OPUS_ROOT, d) for d in ("celt", "silk", "silk/float")]
    )

    if not dnn:
        return [opus_env.SharedObject(f) for f in _base_sources()], public_includes

    dnn_sources = _dnn_sources()
    data_sources = [f for f in dnn_sources if f.endswith("_data.c")]
    weights, extracted, weights_include = opus_dnn.build_weights(env, gen_dir, data_sources)

    opus_env.Append(CPPDEFINES=_DNN_DEFINES)
    # dnn/*.c use root-relative includes like "celt/entenc.h" (upstream's
    # CMakeLists adds the opus root for the same reason).
    opus_env.Append(CPPPATH=[OPUS_ROOT, os.path.join(OPUS_ROOT, "dnn"), weights_include])

    sources = _base_sources()
    sources += [os.path.join(OPUS_ROOT, f) for f in dnn_sources if f not in data_sources]
    objects = [opus_env.SharedObject(f) for f in sources]
    objects += [opus_env.SharedObject(f) for f in extracted]
    # Base sources include the generated *_data.h headers too once the DNN
    # defines are set, so everything waits on the extraction step.
    env.Depends(objects, weights)
    return objects, public_includes
