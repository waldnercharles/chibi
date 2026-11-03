#!/usr/bin/env python3
"""Bind the focused Failure oracle to its frozen source and emitted metadata."""
import hashlib
import json
from pathlib import Path
import re
import sys


def require(condition, message):
    if not condition:
        raise SystemExit(f"owned record failure oracle: {message}")


def digest(content):
    return hashlib.sha256(content).hexdigest()


def expected(source_path, module_path):
    source = source_path.read_bytes()
    module = module_path.read_bytes()
    # This is the intended terminal call, not a match on a runtime message.
    call = b'assert(false, "owned record cell failure releases text and graph roots")'
    require(source.count(call) == 1, "expected exactly one intended terminal assertion")
    start = source.index(call)
    end = start + len(call)
    require(source[start:end].endswith(b")") and source[end:end + 1] == b";",
            "expected the assertion expression range to exclude its semicolon")
    require(source.count(b"Failure :: () -> int {") == 1 and
            source.index(b"Failure :: () -> int {") < start,
            "the intended assertion must belong to Failure")
    text = module.decode("utf-8")
    functions = re.findall(r"^; chibi-function (\d+) Failure$", text, re.M)
    require(len(functions) == 1, "expected one source Failure function identity")
    function = functions[0]
    constants = re.findall(
        r"^@(chibi_scalar\.failure\.(\d+)\.(\d+)) = private unnamed_addr constant "
        r"\{ i64, i32, i32, ptr, i32 \} "
        r"\{ i64 (-?\d+), i32 (\d+), i32 (\d+), ptr [^,]+, i32 \d+ \}$",
        text, re.M)
    selected = [entry for entry in constants if entry[1] == function]
    require(len(selected) == 1, "Failure must emit only its intended assertion metadata")
    symbol, _, _, stable_id, emitted_start, emitted_end = selected[0]
    require((int(emitted_start), int(emitted_end)) == (start, end),
            "emitted Failure range differs from the frozen assertion expression")
    # Entry zero is the selected Failure, rather than another emitted function
    # that merely happens to carry an assertion with the same byte range.
    require(function == "0", "Failure must be the selected native entry")
    bodies = re.findall(r"^define i32 @chibi_scalar\(ptr %ctx\)[^\n]*\n(.*?)^\}", text, re.M | re.S)
    require(len(bodies) == 1, "expected one emitted native entry body")
    stored = re.findall(r"^  store ptr @(chibi_scalar\.failure\.\d+\.\d+), ptr %fail\d+\.at$", bodies[0], re.M)
    require(stored == [symbol], "native entry must store exactly the intended Failure metadata")
    unsigned_id = int(stable_id) & ((1 << 64) - 1)
    return {
        "source_sha256": digest(source), "module_sha256": digest(module),
        "symbol": symbol, "start": start, "end": end,
        "site": f"assertion failed in function {unsigned_id} at source bytes {start} to {end}",
    }


def check_log(receipt, module_path, stdout_path, stderr_path):
    stdout = stdout_path.read_text(encoding="utf-8")
    stderr = stderr_path.read_text(encoding="utf-8")
    require(stdout == "", "native SourceProbe must leave stdout empty")
    lines = stderr.splitlines()
    expected_passes = [
        f"PASS managed source {module_path} force={forced} O{level} poisoned balance"
        for forced in ("false", "true") for level in (0, 2)
    ]
    positions = [index for index, line in enumerate(lines) if line.startswith("PASS managed source ")]
    require([lines[index] for index in positions] == expected_passes,
            "assertion probe must report exactly the four ordered forced/O-level cases")
    # Native SourceProbe uses os.Print to forward each child report to stderr
    # after its PASS line. Keep unrelated diagnostics visible to the controller.
    for ordinal, first in enumerate(positions):
        last = positions[ordinal + 1] if ordinal + 1 < len(positions) else len(lines)
        group = lines[first + 1:last]
        sites = [line for line in group if line.startswith("assertion failed")]
        categories = [line for line in group if line.startswith("native failure:")]
        balances = [line for line in group if line.startswith("managed source live ")]
        require(sites == [receipt["site"]], f"case {ordinal} must fail at the intended source-byte range")
        require(categories == ["native failure: AssertionFailed"], f"case {ordinal} must have exactly the intended failure category")
        require(len(balances) == 1 and re.fullmatch(r"managed source live 0 peak \d+ total \d+", balances[0]),
                f"case {ordinal} must retain its exact live-zero audit")
    ungrouped = lines[:positions[0]]
    require(not any(line.startswith(("assertion failed", "native failure:", "managed source live ", "PASS managed source "))
                    for line in ungrouped), "assertion/audit output appeared outside the four child groups")


def main():
    require(len(sys.argv) in (5, 7), "expected bind/check source module receipt [stdout stderr]")
    mode, source, module, receipt_path = sys.argv[1:5]
    require((mode == "bind" and len(sys.argv) == 5) or (mode == "check" and len(sys.argv) == 7), "invalid verifier mode")
    actual = expected(Path(source), Path(module))
    receipt_file = Path(receipt_path)
    if mode == "bind":
        receipt_file.write_text(json.dumps(actual, indent=2) + "\n", encoding="utf-8")
    else:
        recorded = json.loads(receipt_file.read_text(encoding="utf-8"))
        require(recorded == actual, "frozen source or emitted IR changed after oracle binding")
        check_log(recorded, module, Path(sys.argv[5]), Path(sys.argv[6]))


if __name__ == "__main__":
    main()
