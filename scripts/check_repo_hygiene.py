#!/usr/bin/env python3
"""Reject build outputs, media and caches that must never be committed."""
import subprocess
import sys

FORBIDDEN_SUFFIXES = (
    ".exe", ".dll", ".a", ".lib", ".pdb", ".ilk", ".obj", ".o",
    ".h264", ".264", ".avc", ".h265", ".265", ".hevc",
    ".mp4", ".m4v", ".mov", ".ts", ".m2ts", ".mts", ".mkv", ".webm",
    ".yuv", ".ppm", ".raw", ".log", ".pyc", ".zip", ".ini",
)
FORBIDDEN_DIRS = ("dist", "out", "__pycache__", ".cache")


def tracked_files():
    proc = subprocess.run(["git", "ls-files", "-z"], stdout=subprocess.PIPE,
                          check=True)
    return [p.decode() for p in proc.stdout.split(b"\0") if p]


def main():
    bad = []
    for path in tracked_files():
        lowered = path.lower()
        if lowered.endswith(FORBIDDEN_SUFFIXES):
            bad.append(path)
            continue
        parts = lowered.split("/")
        if any(part in FORBIDDEN_DIRS or part.startswith("build-") or
               part.startswith("cmake-build-") for part in parts[:-1]):
            bad.append(path)
    for path in bad:
        print(f"error: generated or media file is tracked: {path}")
    if bad:
        print("remove it with `git rm --cached` and extend .gitignore")
    print(f"checked {len(tracked_files())} tracked files, {len(bad)} violations")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
