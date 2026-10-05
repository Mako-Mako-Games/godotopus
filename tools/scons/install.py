"""`scons install`: mirror the built addon into the repo's Godot projects.

Copies instead of symlinking so it works on Windows without developer mode.
Destinations that don't exist yet (no project.godot) are skipped.
"""

import os
import shutil

ADDON_DIR = "addons/godotopus"
PROJECTS = ["tests/godot", "demo"]


def _mirror(target, source, env):
    for project in PROJECTS:
        if not os.path.isfile(os.path.join(project, "project.godot")):
            continue
        dest = os.path.join(project, ADDON_DIR)
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        shutil.copytree(ADDON_DIR, dest)
        print("godotopus: installed %s -> %s" % (ADDON_DIR, dest))


def build(env, library):
    target = env.Alias("install", library, _mirror)
    env.AlwaysBuild(target)
    return target
