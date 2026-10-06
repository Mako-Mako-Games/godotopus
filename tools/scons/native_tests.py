"""`scons tests`: builds and runs the native doctest suite for src/core.

The suite links the core sources directly against the same Opus/Speex
objects as the extension, but without godot-cpp, so it runs without Godot.

    scons tests                 build and run
    scons tests sanitize=yes    with AddressSanitizer + UBSan (GCC/Clang)
"""

import glob
import os

OUT_DIR = "build/native_tests"


def _run(target, source, env):
    import subprocess

    exe = source[0].abspath
    print("godotopus: running %s" % exe)
    return subprocess.call([exe])


def build(env, core_sources, dependency_objects, include_dirs, sanitize):
    test_env = env.Clone()
    # The extension env links godot-cpp; the tests must not need it.
    test_env["LIBS"] = []
    test_env.Append(CPPPATH=["src", "tests/native", "third-party/doctest"] + include_dirs)

    # godot-cpp builds without exceptions; doctest's REQUIRE needs them.
    test_env["CXXFLAGS"] = [f for f in test_env["CXXFLAGS"] if f != "-fno-exceptions"]
    test_env["CPPDEFINES"] = [d for d in test_env["CPPDEFINES"] if not (isinstance(d, (tuple, list)) and d[0] == "_HAS_EXCEPTIONS")]
    if test_env.get("is_msvc", False):
        test_env.AppendUnique(CXXFLAGS=["/EHsc"])

    variant = "sanitize" if sanitize else "plain"
    if sanitize:
        if test_env.get("is_msvc", False):
            raise ValueError("sanitize=yes requires GCC or Clang")
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        test_env.Append(CCFLAGS=flags, LINKFLAGS=flags)
        # Static runtimes (MinGW) don't mix with the sanitizer runtimes.
        test_env["LINKFLAGS"] = [f for f in test_env["LINKFLAGS"] if not str(f).startswith("-static")]

    objects = []
    for source in core_sources + sorted(glob.glob("tests/native/*.cpp")):
        name = os.path.splitext(source.replace("\\", "/").replace("/", "_"))[0]
        objects.append(test_env.Object(os.path.join(OUT_DIR, variant, name), source))

    program = test_env.Program(
        os.path.join(OUT_DIR, variant, "godotopus_tests"),
        objects + dependency_objects,
    )
    run = test_env.Alias("tests", program, _run)
    test_env.AlwaysBuild(run)
    return program
