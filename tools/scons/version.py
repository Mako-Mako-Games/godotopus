"""Single source of truth for the addon version: addons/godotopus/plugin.cfg."""

import configparser
import os

PLUGIN_CFG = "addons/godotopus/plugin.cfg"


def read_version():
    parser = configparser.ConfigParser()
    parser.read(PLUGIN_CFG, encoding="utf-8")
    return parser["plugin"]["version"].strip('"')


def _write_header(target, source, env):
    with open(target[0].abspath, "w") as f:
        f.write(
            "#pragma once\n\n"
            "// Generated from %s by tools/scons/version.py. Do not edit.\n"
            '#define GODOTOPUS_VERSION "%s"\n' % (PLUGIN_CFG, env["GODOTOPUS_VERSION"])
        )


def build(env, gen_dir):
    """Generates godotopus_version.h; returns (header_node, include_dir)."""
    version = read_version()
    out = os.path.join(gen_dir, "godotopus_version.h")
    header = env.Command(out, PLUGIN_CFG, _write_header, GODOTOPUS_VERSION=version)
    # Rebuild when the version string changes even if plugin.cfg's other
    # fields don't affect it.
    env.Depends(header, env.Value(version))
    return header, gen_dir
