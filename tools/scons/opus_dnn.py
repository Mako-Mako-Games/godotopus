"""Download, verify and extract the pretrained Opus DNN weights.

libopus's deep-learning features (DRED, deep PLC, OSCE/BWE) need ~130MB of
generated weight sources that are not checked into the opus repository
(upstream fetches them with dnn/download_model.sh). We fetch the archive
pinned by the submodule's autogen.sh, verify its SHA-256, and extract its
generated C sources and headers (none of which exist in the submodule) into
build/gen/opus_dnn/dnn, so the submodule checkout itself is never modified.
"""

import hashlib
import os
import re
import tarfile
import urllib.request

from SCons.Errors import StopError

OPUS_ROOT = "third-party/opus"
AUTOGEN = os.path.join(OPUS_ROOT, "autogen.sh")
URL_TEMPLATE = "https://media.xiph.org/opus/models/opus_data-%s.tar.gz"


def pinned_model_hash():
    with open(AUTOGEN, "r") as f:
        match = re.search(r'download_model\.sh\s+"([0-9a-f]+)"', f.read())
    if not match:
        raise StopError("godotopus: could not find the pinned Opus DNN model hash in " + AUTOGEN)
    return match.group(1)


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_weights_file(name):
    # The archive also holds the PyTorch checkpoints (dnn/models/*.pth),
    # which aren't needed to build.
    return name.startswith("dnn/") and name.endswith((".c", ".h"))


def _fetch_and_extract(target, source, env):
    cache = env["GODOTOPUS_DNN_CACHE"]
    out_root = env["GODOTOPUS_DNN_OUT"]
    stamp = target[0].abspath
    model_hash = pinned_model_hash()
    archive = os.path.join(cache, "opus_data-%s.tar.gz" % model_hash)

    os.makedirs(cache, exist_ok=True)
    if not os.path.isfile(archive):
        url = URL_TEMPLATE % model_hash
        print("godotopus: downloading Opus DNN model weights (~130MB) from %s ..." % url)
        urllib.request.urlretrieve(url, archive)

    print("godotopus: verifying Opus DNN model weights checksum...")
    if _sha256(archive) != model_hash:
        os.remove(archive)
        raise StopError("godotopus: Opus DNN weights failed checksum verification and were deleted. Try again.")

    print("godotopus: extracting Opus DNN weights to %s ..." % out_root)
    with tarfile.open(archive, "r:gz") as tar:
        members = [m for m in tar.getmembers() if m.isfile() and _is_weights_file(m.name)]
        if hasattr(tarfile, "data_filter"):
            tar.extractall(out_root, members=members, filter="data")
        else:
            tar.extractall(out_root, members=members)

    with open(stamp, "w") as f:
        f.write(model_hash + "\n")


def build_weights(env, gen_dir, data_sources):
    """Declares the download/extract step.

    data_sources are the *_data.c paths relative to the opus root (e.g.
    "dnn/plc_data.c"). Returns (stamp_node, extracted_c_paths, include_dir).
    """
    out_root = os.path.join(gen_dir, "opus_dnn")
    stamp_path = os.path.join(out_root, "weights.stamp")
    extracted = [os.path.join(out_root, f) for f in data_sources]

    dnn_env = env.Clone(
        GODOTOPUS_DNN_CACHE=os.path.abspath(cache_dir()),
        GODOTOPUS_DNN_OUT=os.path.abspath(out_root),
    )
    # Every target lives under build/gen, so SCons deleting them before the
    # action runs is harmless (it must never list submodule sources here).
    nodes = dnn_env.Command([stamp_path] + extracted, AUTOGEN, _fetch_and_extract)
    return nodes[0], extracted, os.path.join(out_root, "dnn")


def cache_dir():
    """Where the downloaded archive is kept; CI caches this directory."""
    return os.environ.get("GODOTOPUS_DNN_CACHE", os.path.join("build", "cache", "opus_dnn"))
