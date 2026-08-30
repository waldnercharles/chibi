#!/usr/bin/env python3
"""Validate the small, reviewable evidence contract in a pull request body."""

import re
import sys
from urllib.parse import urlsplit


ARTIFACT_URL = re.compile(r"^/user-attachments/assets/[0-9a-f-]{36}$", re.IGNORECASE)


def field(body: str, name: str) -> str:
    match = re.search(rf"^- {re.escape(name)}:[ \t]*(.*)$", body, re.MULTILINE)
    return match.group(1).strip() if match else ""


def github_artifact_url(value: str) -> bool:
    """Accept only a direct GitHub user-attachment URL, not an arbitrary link."""
    try:
        parsed = urlsplit(value)
    except ValueError:
        return False
    return (
        parsed.scheme == "https"
        and parsed.netloc.lower() == "github.com"
        and not parsed.query
        and not parsed.fragment
        and bool(ARTIFACT_URL.fullmatch(parsed.path))
    )


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

    visual = field(body, "Visual evidence")
    trace = field(body, "Interaction trace")
    if category == "visual":
        if not github_artifact_url(visual):
            return (
                "Visual evidence must be one direct GitHub user-attachment URL "
                "(https://github.com/user-attachments/assets/<uuid>)"
            )
        if not trace or trace.lower().startswith("not applicable"):
            return "Visual evidence requires a computer-use interaction trace"
    elif not visual.lower().startswith("not applicable"):
        return "Automated evidence must mark visual evidence as not applicable"

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
