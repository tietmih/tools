#!/usr/bin/env python3
"""Render a coverage summary in Markdown from a gcovr XML report."""
import argparse
import sys
import xml.etree.ElementTree as ET


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xml")
    ap.add_argument("--tool", default="tool")
    args = ap.parse_args()

    root = ET.parse(args.xml).getroot()
    lines = int(root.attrib.get("lines-valid", "0"))
    hit = int(root.attrib.get("lines-covered", "0"))
    branches = root.attrib.get("branch-rate", "n/a")
    percent = 100.0 * hit / lines if lines else 0.0

    print(f"## Coverage ({args.tool})")
    print()
    print("| Metric | Value |")
    print("|---|---|")
    print(f"| Lines | {hit}/{lines} ({percent:.1f}%) |")
    print(f"| Branch rate | {branches} |")
    print()
    print("Threshold: 60% line coverage.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
