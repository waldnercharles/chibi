#!/usr/bin/env python3
"""Validate the small, reviewable evidence contract in a pull request body."""

import re
import sys


def field(body: str, name: str) -> str:
    match = re.search(rf"^- {re.escape(name)}:[ \t]*(.*)$", body, re.MULTILINE)
    return match.group(1).strip() if match else ""


def error(body: str) -> str | None:
    if "## Evidence" not in body:
        return "PR body must contain an Evidence section"

    category = field(body, "Category").strip("`").lower()
    if category not in {"automated", "visual"}:
        return "Evidence Category must be `automated` or `visual`"

    for name in ("Feature proof", "Tests"):
        value = field(body, name)
        if not value or value.lower() in {"n/a", "tbd", "pending"}:
            return f"Evidence {name} must describe completed proof"

    return None


def main() -> int:
    body = sys.argv[1] if len(sys.argv) == 2 else ""
    message = error(body)
    if message:
        print(message)
        return 1

    print("PR evidence contract passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
