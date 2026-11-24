#!/usr/bin/env python3
"""Check the repository's English/Chinese documentation pairing rule.

Every tracked Markdown document must have a `.zh-CN.md` counterpart, and both
files must link to each other in their header.
"""
import os
import subprocess
import sys


def tracked_markdown(root):
    try:
        proc = subprocess.run(
            ["git", "-C", root, "ls-files", "-z", "--cached", "--others",
             "--exclude-standard", "--", "*.md"],
            stdout=subprocess.PIPE, check=True)
        return sorted(p.decode() for p in proc.stdout.split(b"\0") if p)
    except (OSError, subprocess.CalledProcessError):
        files = []
        for base, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs if d not in (".git", "build", "dist")]
            for name in names:
                if name.endswith(".md"):
                    files.append(os.path.relpath(os.path.join(base, name),
                                                 root))
        return sorted(files)


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    files = tracked_markdown(root)
    tracked = set(files)
    errors = []

    for path in files:
        if path.endswith(".zh-CN.md"):
            base = path[: -len(".zh-CN.md")] + ".md"
            if base not in tracked:
                errors.append(f"{path}: missing English counterpart {base}")
                continue
            counterpart = os.path.basename(base)
        else:
            base = path
            zh = path[:-3] + ".zh-CN.md"
            if zh not in tracked:
                errors.append(f"{path}: missing Chinese counterpart {zh}")
                continue
            counterpart = os.path.basename(zh)

        with open(os.path.join(root, path), encoding="utf-8") as f:
            header = [next(f, "") for _ in range(6)]
        if not any(counterpart in line for line in header):
            errors.append(f"{path}: no language switch link in the first lines")

    for error in errors:
        print(error)
    print(f"checked {len(files)} Markdown files, {len(errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
