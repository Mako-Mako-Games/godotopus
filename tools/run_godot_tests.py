#!/usr/bin/env python
"""Runs the gdUnit4 suite in tests/godot against the installed addon.

    python tools/run_godot_tests.py --godot /path/to/godot

The Godot binary can also come from the GODOT_BIN environment variable.
Run `scons install` and `python tools/fetch_gdunit4.py` first.
"""

import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, "tests", "godot")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--godot", default=os.environ.get("GODOT_BIN"), help="Godot executable (default: $GODOT_BIN)")
    parser.add_argument("--fresh", action="store_true", help="delete the project's .godot cache first")
    args = parser.parse_args()

    if not args.godot:
        parser.error("pass --godot or set GODOT_BIN")
    for required in ("addons/godotopus/godotopus.gdextension", "addons/gdUnit4/plugin.cfg"):
        if not os.path.isfile(os.path.join(PROJECT, required)):
            parser.error("tests/godot/%s is missing; run `scons install` and `python tools/fetch_gdunit4.py`" % required)

    if args.fresh:
        shutil.rmtree(os.path.join(PROJECT, ".godot"), ignore_errors=True)

    # Importing registers the extension and the test scripts' classes. Its
    # exit code is ignored: the import itself is not under test, and Godot
    # 4.7 crashes on exit after a fresh import that loads any MinGW-built
    # godot-cpp extension registering a class (reproduced with a one-class
    # extension; not caused by this addon).
    subprocess.call([args.godot, "--headless", "--path", PROJECT, "--import"])

    command = [
        args.godot,
        "--headless",
        "--path",
        PROJECT,
        # Refuses the debugger connection so a script error can't hang the run.
        "--remote-debug",
        "tcp://127.0.0.1:0",
        "-s",
        "res://addons/gdUnit4/bin/GdUnitCmdTool.gd",
        "--ignoreHeadlessMode",
        "-a",
        "res://tests",
    ]
    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main())
