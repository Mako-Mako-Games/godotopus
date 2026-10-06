#!/usr/bin/env python
"""Installs the pinned gdUnit4 release into tests/godot/addons/gdUnit4.

gdUnit4 isn't committed to the repo; run this once before running the Godot
test suite (CI does the same).

    python tools/fetch_gdunit4.py
"""

import io
import os
import shutil
import sys
import urllib.request
import zipfile

VERSION = "v6.2.1"  # Supports Godot 4.5 - 4.7.1.
URL = "https://github.com/MikeSchulze/gdUnit4/archive/refs/tags/%s.zip" % VERSION
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "tests", "godot", "addons", "gdUnit4")
STAMP = os.path.join(DEST, ".version")


def main():
    if os.path.isfile(STAMP) and open(STAMP).read().strip() == VERSION:
        print("gdUnit4 %s is already installed." % VERSION)
        return 0

    print("Downloading gdUnit4 %s ..." % VERSION)
    with urllib.request.urlopen(URL) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))

    if os.path.isdir(DEST):
        shutil.rmtree(DEST)

    # Archive layout: gdUnit4-<version>/addons/gdUnit4/...; its own test suite is skipped.
    for member in archive.infolist():
        parts = member.filename.split("/", 3)
        if len(parts) < 4 or parts[1:3] != ["addons", "gdUnit4"] or member.is_dir():
            continue
        relative = parts[3]
        if relative.startswith("test/"):
            continue
        target = os.path.join(DEST, *relative.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with archive.open(member) as source, open(target, "wb") as out:
            shutil.copyfileobj(source, out)

    with open(STAMP, "w") as f:
        f.write(VERSION + "\n")
    print("Installed gdUnit4 %s into %s" % (VERSION, os.path.relpath(DEST, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
