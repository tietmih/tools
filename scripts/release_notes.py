#!/usr/bin/env python3
"""Combine the English and Chinese release notes into one Release body."""
import argparse
import os
import re
import sys

LINK = re.compile(r"\]\(([^)\s]+)\)")
SWITCH_LINE = re.compile(r"(English.*简体中文|\[English\].*简体中文)")


def strip_switch(text):
    lines = text.splitlines()
    out = [line for i, line in enumerate(lines)
           if not (i < 6 and SWITCH_LINE.search(line))]
    while out and not out[0].strip():
        out.pop(0)
    text = re.sub(r"\n{3,}", "\n\n", "\n".join(out))
    return text.rstrip() + "\n"


def rewrite_links(text, base_dir, repo, tag):
    def replace(match):
        target = match.group(1)
        if re.match(r"^(?:[a-z][a-z0-9+.-]*:|#|/)", target):
            return match.group(0)
        path, sep, anchor = target.partition("#")
        if not path:
            return match.group(0)
        resolved = os.path.normpath(os.path.join(base_dir, path))
        if resolved.startswith(".."):
            return match.group(0)
        url = f"https://github.com/{repo}/blob/{tag}/{resolved}"
        return f"]({url}{sep}{anchor})"

    return LINK.sub(replace, text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tool")
    ap.add_argument("version")
    ap.add_argument("--repo", required=True, help="owner/repository")
    ap.add_argument("--tag", required=True)
    args = ap.parse_args()

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    base = os.path.join(args.tool, "docs", "releases")
    english_path = os.path.join(root, base, f"v{args.version}.md")
    chinese_path = os.path.join(root, base, f"v{args.version}.zh-CN.md")
    for path in (english_path, chinese_path):
        if not os.path.isfile(path):
            sys.stderr.write(f"error: missing {path}\n")
            return 1

    english = rewrite_links(strip_switch(open(english_path,
                                              encoding="utf-8").read()),
                            base, args.repo, args.tag)
    chinese = rewrite_links(strip_switch(open(chinese_path,
                                              encoding="utf-8").read()),
                            base, args.repo, args.tag)

    # Drop the duplicated title from the Chinese section.
    if english.startswith("# ") and chinese.startswith("# "):
        chinese = chinese.split("\n", 1)[1].lstrip("\n")

    print(english)
    print("---\n")
    print("## 简体中文\n")
    print(chinese)
    return 0


if __name__ == "__main__":
    sys.exit(main())
