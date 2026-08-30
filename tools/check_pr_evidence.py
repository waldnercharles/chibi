#!/usr/bin/env python3
"""Validate the small, reviewable evidence contract in a pull request body."""

import re
import sys


def field(body: str, name: str) -> str:
    match = re.search(rf"^- {re.escape(name)}:\s*(.*)$", body, re.MULTILINE)
    return match.group(1).strip() if match else ""


def main() -> int:
    body = sys.argv[1] if len(sys.argv) == 2 else ""
    if "## Evidence" not in body:
        print("PR body must contain an Evidence section")
        return 1

    category = field(body, "Category").strip("`").lower()
    if category not in {"automated", "visual"}:
        print("Evidence Category must be `automated` or `visual`")
        return 1

    for name in ("Feature proof", "Tests"):
        value = field(body, name)
        if not value or value.lower() in {"n/a", "tbd", "pending"}:
            print(f"Evidence {name} must describe completed proof")
            return 1

    visual = field(body, "Visual evidence")
    trace = field(body, "Interaction trace")
    if category == "visual":
        if not re.search(r"https?://\S+", visual):
            print("Visual evidence must contain an attached HTTP(S) artifact URL")
            return 1
        if not trace or trace.lower().startswith("not applicable"):
            print("Visual evidence requires a computer-use interaction trace")
            return 1
    elif not visual.lower().startswith("not applicable"):
        print("Automated evidence must mark visual evidence as not applicable")
        return 1

    print("PR evidence contract passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
