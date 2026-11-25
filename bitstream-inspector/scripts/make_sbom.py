#!/usr/bin/env python3
"""Generate a CycloneDX 1.6 SBOM for the Windows package.

The dependency set is small and pinned, so the SBOM is built from the same
files that configure the build (deps.env, BUILD-INFO) plus the hashes of the
actual DLLs and release assets.
"""
import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import uuid

APP_NAME = "Bitstream Inspector"
PROJECT_URL = "https://github.com/tietmih/tools"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def first_line(cmd):
    try:
        out = subprocess.run(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True,
                             timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return out.splitlines()[0].strip() if out else ""


def tool_component(name, line):
    if not line:
        return None
    match = re.search(r"\d+(?:\.\d+)*", line)
    version = match.group(0) if match else line
    return {"type": "application", "name": name, "version": version}


def read_env(path):
    values = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    return values


def ffmpeg_configure(build_info):
    flags = []
    with open(build_info, encoding="utf-8") as f:
        for line in f:
            match = re.match(r"^#\s+(--.*)$", line.rstrip())
            if match:
                flags.append(match.group(1).strip())
    return " ".join(flags)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--dist", required=True)
    ap.add_argument("--dll-dir", required=True)
    ap.add_argument("--deps", required=True)
    ap.add_argument("--build-info", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--commit", default=os.environ.get("GITHUB_SHA", ""))
    ap.add_argument("--makensis",
                    default=os.environ.get("MAKENSIS") or
                    shutil.which("makensis") or "makensis")
    args = ap.parse_args()

    deps = read_env(args.deps)
    ffmpeg_version = deps.get("FFMPEG_VERSION")
    ffmpeg_hash = deps.get("FFMPEG_SHA256")
    if not ffmpeg_version or not ffmpeg_hash:
        sys.stderr.write("error: FFMPEG_VERSION/SHA256 missing in deps.env\n")
        return 1

    dlls = sorted(name for name in os.listdir(args.dll_dir)
                  if name.endswith(".dll"))
    if len(dlls) < 4:
        sys.stderr.write("error: expected at least four FFmpeg DLLs\n")
        return 1
    dll_components = []
    for name in dlls:
        dll_components.append({
            "type": "file",
            "name": name,
            "hashes": [{"alg": "SHA-256",
                        "content": sha256(os.path.join(args.dll_dir, name))}],
        })

    installer = os.path.join(
        args.dist, f"BitstreamInspector-{args.version}-win64-setup.exe")
    portable = os.path.join(
        args.dist, f"BitstreamInspector-{args.version}-win64-portable.zip")
    for path in (installer, portable):
        if not os.path.isfile(path):
            sys.stderr.write(f"error: missing {path}\n")
            return 1

    tools = []
    for name, cmd in (
            ("gcc (mingw-w64)", ["x86_64-w64-mingw32-g++", "-dumpversion"]),
            ("cmake", ["cmake", "--version"]),
            ("nsis", [args.makensis, "-VERSION"])):
        component = tool_component(name, first_line(cmd))
        if component:
            tools.append(component)

    serial = uuid.uuid5(
        uuid.NAMESPACE_URL,
        f"{PROJECT_URL}/releases/tag/bitstream-inspector-v{args.version}")
    timestamp = datetime.datetime.now(
        datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    app_ref = f"pkg:generic/bitstream-inspector@{args.version}"
    ffmpeg_ref = f"pkg:generic/ffmpeg@{ffmpeg_version}"

    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{serial}",
        "version": 1,
        "metadata": {
            "timestamp": timestamp,
            "tools": {"components": tools},
            "component": {
                "type": "application",
                "bom-ref": app_ref,
                "name": APP_NAME,
                "version": args.version,
                "licenses": [{"license": {"id": "MIT"}}],
                "externalReferences": [
                    {"type": "vcs", "url": PROJECT_URL},
                    {"type": "distribution",
                     "url": f"{PROJECT_URL}/releases/tag/bitstream-inspector-v{args.version}"},
                ],
            },
            "properties": [
                {"name": "build:host", "value": platform.platform()},
                {"name": "build:commit", "value": args.commit or "unknown"},
            ],
        },
        "components": [
            {
                "type": "library",
                "bom-ref": ffmpeg_ref,
                "name": "FFmpeg",
                "version": ffmpeg_version,
                "licenses": [{"license": {"id": "LGPL-2.1-or-later"}}],
                "hashes": [{"alg": "SHA-256", "content": ffmpeg_hash}],
                "externalReferences": [{
                    "type": "distribution",
                    "url": f"https://ffmpeg.org/releases/ffmpeg-{ffmpeg_version}.tar.xz",
                }],
                "properties": [{
                    "name": "build:ffmpeg-configure",
                    "value": ffmpeg_configure(args.build_info),
                }],
                "components": dll_components,
            },
            {
                "type": "file",
                "name": os.path.basename(installer),
                "hashes": [{"alg": "SHA-256", "content": sha256(installer)}],
            },
            {
                "type": "file",
                "name": os.path.basename(portable),
                "hashes": [{"alg": "SHA-256", "content": sha256(portable)}],
            },
        ],
        "dependencies": [{
            "ref": app_ref,
            "dependsOn": [ffmpeg_ref],
        }],
    }

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(bom, f, indent=2)
        f.write("\n")
    print(f"SBOM written: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
