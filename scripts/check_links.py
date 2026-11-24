#!/usr/bin/env python3
"""Check that relative links in tracked Markdown files exist."""
import os
import re
import subprocess
import sys

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")


def markdown_files(root):
    proc = subprocess.run(
        ["git", "-C", root, "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", "*.md"],
        stdout=subprocess.PIPE, check=True)
    return sorted(p.decode() for p in proc.stdout.split(b"\0") if p)


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    failures = []
    files = markdown_files(root)
    for path in files:
        base = os.path.dirname(path)
        with open(os.path.join(root, path), encoding="utf-8") as f:
            text = f.read()
        for match in LINK.finditer(text):
            target = match.group(1)
            if re.match(r"^(?:[a-z][a-z0-9+.-]*:|#|/)", target):
                continue
            rel = target.split("#", 1)[0]
            if not rel:
                continue
            resolved = os.path.normpath(os.path.join(base, rel))
            if not os.path.exists(os.path.join(root, resolved)):
                failures.append(f"{path}: broken link {target}")

    for failure in failures:
        print(failure)
    print(f"checked {len(files)} Markdown files, {len(failures)} broken links")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
