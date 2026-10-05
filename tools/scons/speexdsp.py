"""SpeexDSP resampler build (only resample.c is used)."""

import os

SPEEX_ROOT = "third-party/speexdsp"

# Must match between the Speex objects and any plugin source that includes
# speex_resampler.h: RANDOM_PREFIX renames every exported resampler symbol so
# it can't collide with another copy of SpeexDSP linked into the same process.
SPEEX_DEFINES = [
    "OUTSIDE_SPEEX",
    "RANDOM_PREFIX=godotopus",
    "FLOATING_POINT",
    "EXPORT=",
]


def _write_config_types(target, source, env):
    # Normally produced by autoconf. Generated under build/gen so the
    # submodule checkout is never modified.
    with open(target[0].abspath, "w") as f:
        f.write(
            "#ifndef SPEEXDSP_CONFIG_TYPES_H\n"
            "#define SPEEXDSP_CONFIG_TYPES_H\n\n"
            "#include <stdint.h>\n\n"
            "typedef int16_t spx_int16_t;\n"
            "typedef uint16_t spx_uint16_t;\n"
            "typedef int32_t spx_int32_t;\n"
            "typedef uint32_t spx_uint32_t;\n\n"
            "#endif /* SPEEXDSP_CONFIG_TYPES_H */\n"
        )


def build(env, gen_dir):
    """Returns (objects, include_dirs) for the Speex resampler."""
    config_dir = os.path.join(gen_dir, "speexdsp")
    config_types = env.Command(
        os.path.join(config_dir, "speexdsp_config_types.h"),
        os.path.join(SPEEX_ROOT, "include/speex/speexdsp_config_types.h.in"),
        _write_config_types,
    )

    include_dirs = [os.path.join(SPEEX_ROOT, "include"), config_dir]

    speex_env = env.Clone()
    speex_env.Append(CPPDEFINES=SPEEX_DEFINES)
    speex_env.Append(
        CPPPATH=include_dirs
        + [
            os.path.join(SPEEX_ROOT, "libspeexdsp"),
            os.path.join(SPEEX_ROOT, "include/speex"),
        ]
    )

    obj = speex_env.SharedObject(os.path.join(SPEEX_ROOT, "libspeexdsp/resample.c"))
    env.Depends(obj, config_types)
    return [obj], include_dirs, config_types
