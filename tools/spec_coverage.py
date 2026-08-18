#!/usr/bin/env python3
"""Check that every docs/spec.md requirement is claimed and tested.

Implements the mechanism from docs/adr/0007. Four checks:

  1. Every spec requirement ID is claimed by a docs/roadmap.md phase.
  2. Every ID cited by roadmap.md or test/self_check.c is defined in spec.md,
     so a typo in an ID cannot hide a requirement.
  3. Every ID claimed by a *completed* roadmap phase has a citing check name in
     test/self_check.c. A phase is completed when its heading is marked
     "(done)".
  4. No requirement is cited by an implausible number of checks. Checks 1 to 3
     count citations without judging whether a citation is apt, so prefixing a
     pile of unrelated tests with one convenient ID satisfies them while
     destroying the signal. That is worse than no check, because it
     manufactures confidence. Check 4 makes the shape of that abuse visible.

Run: python3 tools/spec_coverage.py   (or: make spec-check)
Exit status is non-zero on any failure, so it works as a CI or hook step.
"""

import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "docs" / "spec.md"
ROADMAP = ROOT / "docs" / "roadmap.md"
SELF_CHECK = ROOT / "test" / "self_check.c"
MANUAL = ROOT / "docs" / "manual-verification.md"

ID = r"[A-Z]+-\d+"

# A check name leads with the requirement it exercises: check(..., "RUN-4: ...").
CHECK_NAME = re.compile(rf'"({ID}):')

# One requirement legitimately needs a handful of checks. Dozens means the ID was
# used as a bucket to satisfy this tool rather than to describe what the test
# exercises.
MAX_CHECKS_PER_ID = 8

# IDs that genuinely warrant more, each with the reason it is exempt.
BROAD_IDS: dict[str, str] = {
    # "FMT-12": "round-trip is asserted once per pin type and per edge kind",
}


def defined_ids(spec_text):
    """Requirement IDs, in spec order.

    A definition starts its line and is followed by the requirement sentence, so
    the next characters are a space then either a capital or a backtick opening
    a code span, as in "`TYPE-10` `PIN_F32` and `PIN_F64` are one Real
    category". A cross-reference that happens to wrap onto a line start is
    followed by punctuation or a lowercase word, as in "`UI-7` and `RUN-14`",
    and is not a definition.
    """
    ids = re.findall(rf"^`({ID})` [A-Z`]", spec_text, re.M)
    dupes = sorted({i for i in ids if ids.count(i) > 1})
    withdrawn = set(re.findall(rf"^`({ID})` WITHDRAWN", spec_text, re.M))
    return ids, dupes, withdrawn


def manually_verified(text):
    """IDs recorded as human-verified in docs/manual-verification.md.

    A requirement that needs a window cannot be covered by self_check, and
    calling it "manual" in the roadmap leaves no evidence it was ever checked.
    An entry names the ID, the date, and the commit it was verified against, so
    a stale claim is visible rather than implied.
    """
    return set(re.findall(rf"^\|\s*`({ID})`", text, re.M))


def check_citations(check_text):
    """How many checks lead with each requirement ID."""
    return collections.Counter(CHECK_NAME.findall(check_text))


def cited_ids(text):
    """IDs mentioned in backticks, expanding 'X-1 through X-9' ranges."""
    found = set(re.findall(rf"`({ID})`", text))
    ranges = re.findall(rf"`([A-Z]+)-(\d+)`\s+(?:through|to)\s+`([A-Z]+)-(\d+)`", text)
    for prefix, first, prefix2, last in ranges:
        if prefix == prefix2:
            found |= {f"{prefix}-{i}" for i in range(int(first), int(last) + 1)}
    return found


def covers_block(section):
    """The IDs on a phase's `Covers:` line, which may wrap several lines.

    Only this block counts as a phase claiming a requirement. A phase's prose
    mentions IDs for other reasons, such as explaining that a requirement moved
    to a later phase, and counting those as claims makes the explanation itself
    trip the gate.
    """
    match = re.search(r"^Covers:(.*?)(?:\n\s*\n)", section, re.S | re.M)
    return cited_ids(match.group(1)) if match else set()


def phase_sections(roadmap_text):
    for section in re.split(r"^## ", roadmap_text, flags=re.M)[1:]:
        yield section.split("\n", 1)[0], section


def completed_phase_ids(roadmap_text):
    """IDs a phase marked (done) claims on its Covers line."""
    covered = set()
    for heading, section in phase_sections(roadmap_text):
        if "(done)" in heading.lower():
            covered |= covers_block(section)
    return covered


def main():
    for path in (SPEC, ROADMAP):
        if not path.exists():
            print(f"missing {path.relative_to(ROOT)}", file=sys.stderr)
            return 2

    spec_text = SPEC.read_text()
    roadmap_text = ROADMAP.read_text()
    check_text = SELF_CHECK.read_text() if SELF_CHECK.exists() else ""

    manual_text = MANUAL.read_text() if MANUAL.exists() else ""

    citations = check_citations(check_text)
    ids, dupes, withdrawn = defined_ids(spec_text)
    defined = set(ids)
    claimed = cited_ids(roadmap_text)
    tested = cited_ids(check_text) | set(re.findall(rf"\b({ID})\b", check_text))
    verified = manually_verified(manual_text)
    done = completed_phase_ids(roadmap_text)

    failures = []

    if dupes:
        failures.append(("duplicate spec IDs", dupes))

    unclaimed = [i for i in ids if i not in claimed]
    if unclaimed:
        failures.append(("defined in spec.md, claimed by no roadmap phase", unclaimed))

    ghosts = sorted((claimed | tested | verified) - defined)
    if ghosts:
        failures.append(("cited but not defined in spec.md", ghosts))

    # A withdrawn requirement keeps its number so it is never reused, but it has
    # no behaviour left to test.
    covered = tested | verified | withdrawn
    untested = sorted(i for i in done if i not in covered)
    if untested:
        failures.append((
            "claimed by a completed phase, with no citing check and no manual\n"
            "  verification record. Either write the check, record the human\n"
            "  verification in docs/manual-verification.md, or move the requirement to\n"
            "  the phase that can actually satisfy it",
            untested,
        ))

    stale = sorted(verified & tested)
    if stale:
        failures.append((
            "recorded as manually verified but also covered by a check, so the manual\n"
            "  record is redundant and will rot; drop the manual entry",
            stale,
        ))

    crowded = [
        f"{rid} leads {n} check names (limit {MAX_CHECKS_PER_ID})"
        for rid, n in citations.most_common()
        if n > MAX_CHECKS_PER_ID and rid not in BROAD_IDS
    ]
    if crowded:
        failures.append((
            "one requirement leads too many check names, which is what bulk-prefixing\n"
            "  unrelated tests looks like. Recite each check against the requirement it\n"
            "  actually exercises, or add the ID to BROAD_IDS with a reason",
            crowded,
        ))

    print(f"spec requirements:   {len(ids)}")
    print(f"claimed by roadmap:  {len(defined & claimed)}")
    print(f"in completed phases: {len(done & defined)}")
    print(f"cited by self_check: {len(defined & tested)}")
    print(f"manually verified:   {len(defined & verified)}")
    print(f"withdrawn:           {len(withdrawn)}")
    print(f"check names:         {sum(citations.values())}")
    if citations:
        top = ", ".join(f"{r}x{n}" for r, n in citations.most_common(5))
        print(f"most-cited:          {top}")

    if not failures:
        print("\nspec coverage OK")
        return 0

    for title, items in failures:
        print(f"\n{title} ({len(items)}):", file=sys.stderr)
        for item in items:
            print(f"  {item}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
