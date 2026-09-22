"""Retrieve editor identities; display names never authorize an edit."""

import difflib
import re
from pathlib import PurePosixPath


def words(text):
    text = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", text)
    return re.findall(r"[a-z0-9]+", text.lower())


def tokens(text):
    return set(words(text))


def name_match(name, query):
    needle, haystack = words(name), words(query)
    joined = " ".join(haystack)
    exact = bool(needle) and any(haystack[i:i+len(needle)] == needle for i in range(len(haystack)))
    overlap = len(set(needle) & set(haystack)) / max(1, len(set(needle)))
    fuzzy = max((difflib.SequenceMatcher(None, "".join(needle), "".join(haystack[i:i+n])).ratio()
                 for n in range(1, len(needle) + 2) for i in range(len(haystack))), default=0)
    return (3 + len(needle) / 100 if exact else max(overlap, fuzzy if fuzzy >= .72 else 0))


def object_match(obj, query):
    if obj['kind'] in {'entity', 'node'}:
        return name_match(obj['name'], query)
    return max(name_match(obj['name'], query),
               name_match(PurePosixPath(obj.get('path', '')).stem, query))


def retrieve(catalog, query, kinds=(), limit=6):
    objects = catalog.get("objects", [])
    terms = tokens(query)
    explicit = terms & {o["kind"] for o in objects}
    if "entity" in terms or "instance" in terms or "instances" in terms:
        explicit = {"entity"}
    allowed = set(kinds)
    if explicit:
        allowed = allowed & explicit if allowed else explicit
        if not allowed:
            return []
    scored = []
    for obj in objects:
        if allowed and obj["kind"] not in allowed:
            continue
        score = object_match(obj, query)
        evidence = ["name"] if score else []
        context = " ".join([obj.get("prototype", ""), *obj.get("components", [])])
        related = tokens(context) & terms
        if related:
            score += min(.45, len(related) * .15)
            evidence.append("components")
        # Expand only real references. A relationship adds evidence, not identity.
        frontier = {obj["id"]}
        seen = set(frontier)
        for _ in range(2):
            adjacent = set()
            for ref in catalog.get("relationships", []):
                if ref["target"] in frontier:
                    adjacent.add(ref["source"])
                if ref["source"] in frontier:
                    adjacent.add(ref["target"])
            adjacent -= seen
            for other in objects:
                if other["id"] in adjacent and object_match(other, query) >= 3:
                    score += .5
                    evidence.append("reference: " + other["name"])
            frontier = adjacent
            seen |= adjacent
        if score:
            scored.append({**obj, "match": round(score, 4), "evidence": evidence})
    return sorted(scored, key=lambda o: (-o["match"], o["id"]))[:limit]


def label(obj):
    return " · ".join(filter(None, (obj["name"], obj["kind"], obj.get("prototype"), obj.get("path"))))

