#!/usr/bin/env python3
"""Check corpus bookkeeping and the C fixture only. Never invokes Chibi."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="existing C compiler executable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "slice_cases.json").read_text())
    if manifest["version"] != 1:
        raise ValueError("unsupported slice_cases.json version")
    identifiers, files = set(), set()
    calls = rejects = 0
    for case in manifest["cases"]:
        if case["id"] in identifiers:
            raise ValueError(f"duplicate case: {case['id']}")
        identifiers.add(case["id"])
        texts = []
        for name in case["files"]:
            path = (root / name).resolve()
            if not path.is_relative_to(root / "slice_cases"):
                raise ValueError(f"source outside slice_cases: {name}")
            texts.append(path.read_text())
            files.add(path)
        if ("calls" in case) == ("reject" in case):
            raise ValueError(f"case needs calls or reject: {case['id']}")
        if "reject" in case:
            stage, reason, anchor = case["reject"]
            if stage not in {"type", "resolve", "subset", "link"} or not reason or anchor not in texts[0]:
                raise ValueError(f"invalid diagnostic expectation: {case['id']}")
            rejects += 1
        else:
            for symbol, arguments, expected in case["calls"]:
                if f"pub {symbol} ::" not in texts[0]:
                    raise ValueError(f"missing caller entry spelling: {case['id']}.{symbol}")
                for value in [*arguments, expected]:
                    if type(value) not in (bool, int) or not -(1 << 63) <= value < (1 << 63):
                        raise ValueError(f"invalid scalar expectation: {case['id']}.{symbol}")
                calls += 1
        if "foreign_fixture" in case and case["foreign_fixture"] != "slice_probe.c":
            raise ValueError(f"unknown C fixture: {case['id']}")
    if files != set((root / "slice_cases").rglob("*.chibi")):
        raise ValueError("unregistered or missing Chibi fixture")
    compiler = shutil.which(args.cc)
    if not compiler:
        raise RuntimeError(f"existing C compiler unavailable: {args.cc}")
    with tempfile.TemporaryDirectory(prefix="chibi-slice-") as temporary:
        executable = Path(temporary) / "probe"
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-DCHIBI_SLICE_PROBE_SELFTEST", str(root / "slice_probe.c"), "-o", str(executable)]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=10)
    print(json.dumps({"corpus_bookkeeping": "passed", "source_files": len(files),
                      "positive_calls": calls, "negative_cases": rejects, "c_fixture_selftest": "passed",
                      "chibi_parse_typecheck_jit_aot": "not_run", "compiler": compiler}, sort_keys=True))


if __name__ == "__main__":
    main()
