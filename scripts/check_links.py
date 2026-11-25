#!/usr/bin/env python3
"""Check relative links in tracked Markdown files and local anchors."""
import os
import re
import subprocess
import sys

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
HEADING = re.compile(r"^#{1,6}\s+(.+?)\s*#*\s*$")


def markdown_files(root):
    proc = subprocess.run(
        ["git", "-C", root, "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", "*.md"],
        stdout=subprocess.PIPE, check=True)
    return sorted(p.decode() for p in proc.stdout.split(b"\0") if p)


def strip_markup(text):
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[*_]{1,3}([^*_]+)[*_]{1,3}", r"\1", text)
    return text.strip()


def slug(text):
    text = strip_markup(text).lower()
    out = []
    for ch in text:
        if ch.isalnum() or ch in "-_":
            out.append(ch)
        elif ch.isspace():
            out.append("-")
    # GitHub keeps repeated hyphens; trim leading/trailing ones.
    return "".join(out).strip("-")


def anchors(path):
    found = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = HEADING.match(line.rstrip("\n"))
            if not m:
                continue
            base = slug(m.group(1))
            if not base:
                continue
            count = found.get(base, 0)
            found[base] = count + 1
            if count:
                found[f"{base}-{count}"] = 0
    return set(found)


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    files = markdown_files(root)
    failures = []
    anchor_cache = {}
    for path in files:
        base = os.path.dirname(path)
        with open(os.path.join(root, path), encoding="utf-8") as f:
            text = f.read()
        for match in LINK.finditer(text):
            target = match.group(1)
            if re.match(r"^(?:[a-z][a-z0-9+.-]*:|/)", target):
                continue
            rel, _, anchor = target.partition("#")
            if not rel:
                target_path = path
            else:
                target_path = os.path.normpath(os.path.join(base, rel))
                if not os.path.exists(os.path.join(root, target_path)):
                    failures.append(f"{path}: broken link {target}")
                    continue
            if not anchor or not target_path.endswith(".md"):
                continue
            if target_path not in anchor_cache:
                anchor_cache[target_path] = anchors(
                    os.path.join(root, target_path))
            if anchor not in anchor_cache[target_path]:
                failures.append(
                    f"{path}: missing anchor #{anchor} in {target_path}")

    for failure in failures:
        print(failure)
    print(f"checked {len(files)} Markdown files, {len(failures)} broken links")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
