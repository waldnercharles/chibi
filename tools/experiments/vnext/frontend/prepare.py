#!/usr/bin/env python3
"""Prepare an isolated current-parser probe. Never launches Chibi or LLVM."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PARSER_FILES = (
    "source_parser/Declarations.chibi", "source_parser/Expressions.chibi",
    "source_parser/Parser.chibi", "source_parser/Statements.chibi",
    "source_parser/Tree.chibi", "source_parser/Types.chibi",
    "source_tokenizer/Tokenizer.chibi", "syntax_diagnostics/Diagnostics.chibi",
)
IMPLEMENTATION_FILES = ("ParserHarness.chibi.in", "prepare.py", "run.py", "tests.py", "owned_process.py")
IMPORTS = {'import tk "project:source_tokenizer"', 'import syntax "project:syntax_diagnostics"'}


def identity(path):
    digest = hashlib.sha256()
    size = 0
    buffer = bytearray(256 * 1024)
    with path.open("rb") as stream:
        while count := stream.readinto(buffer):
            digest.update(memoryview(buffer)[:count])
            size += count
    return {"bytes": size, "sha256": digest.hexdigest()}


def byte_identity(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def literal(text):
    # Chibi and JSON share these escapes; keep UTF-8 literal instead of \uXXXX.
    if any(ord(c) < 32 and c not in "\n\r\t" for c in text):
        raise ValueError("unsupported control byte in source fixture")
    return json.dumps(text, ensure_ascii=False)


def prepare(output):
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    tree = subprocess.check_output(["git", "rev-parse", f"{head}^{{tree}}"], cwd=ROOT, text=True).strip()
    manifest_path = HERE.parent / "slice_cases.json"
    registry_bytes = manifest_path.read_bytes()
    manifest = json.loads(registry_bytes)
    if manifest["version"] != 1:
        raise ValueError("unrecognized corpus registry version")
    files = list(dict.fromkeys(path for case in manifest["cases"] for path in case["files"]))
    calls, corpus = [], []
    for index, relative in enumerate(files):
        source = (HERE.parent / relative).resolve()
        if not source.is_relative_to(HERE.parent) or source.suffix != ".chibi":
            raise ValueError(f"corpus source outside experiment: {relative}")
        data = source.read_bytes()
        text = data.decode("utf-8")
        calls.append(f"    parsed_{index} := CheckParsed({literal(text)});\n    assert(parsed_{index}, {literal(relative)});")
        corpus.append({"path": relative, **byte_identity(data)})
    # Check the direct closure before creating output. No source parser is
    # implemented here: this only fences the eight named compiler input files.
    actual = sorted(str(p.relative_to(ROOT / "tools/compiler"))
                    for package in ("source_parser", "source_tokenizer", "syntax_diagnostics")
                    for p in (ROOT / "tools/compiler" / package).glob("*.chibi"))
    if actual != sorted(PARSER_FILES):
        raise ValueError("parser source closure changed; inspect before expanding the fence")
    sources, source_bytes = [], {}
    for relative in PARSER_FILES:
        path = ROOT / "tools/compiler" / relative
        committed = subprocess.check_output(["git", "show", f"{head}:tools/compiler/{relative}"], cwd=ROOT)
        data = path.read_bytes()
        if data != committed:
            raise ValueError(f"parser source differs from named commit: {relative}")
        imports = set(re.findall(r"^import .+$", data.decode("utf-8"), re.MULTILINE))
        if not imports <= IMPORTS:
            raise ValueError(f"unexpected dependency in {relative}: {imports - IMPORTS}")
        sources.append({"path": relative, **byte_identity(data)})
        source_bytes[relative] = data
    implementation = {name: ((HERE.parent if name == "owned_process.py" else HERE) / name).read_bytes()
                      for name in IMPLEMENTATION_FILES}
    template = implementation["ParserHarness.chibi.in"].decode("utf-8")
    if template.count("    // CORPUS_CALLS") != 1:
        raise ValueError("harness must have exactly one corpus insertion marker")
    generated = template.replace("    // CORPUS_CALLS", "\n".join(calls))
    output.mkdir(parents=True, exist_ok=False)
    for row in sources:
        destination = output / "compiler" / row["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(source_bytes[row["path"]])
        if identity(destination) != {k: row[k] for k in ("bytes", "sha256")}:
            raise ValueError(f"source changed while copying: {row['path']}")
    harness = output / "ParserHarness.chibi"
    harness.write_text(generated)
    (output / "controller").mkdir()
    for name, data in implementation.items():
        (output / "controller" / name).write_bytes(data)
    # --core is mandatory to the driver but this closure imports no core package.
    (output / "empty-core").mkdir()
    fence = {
        "schema": 1, "purpose": "current parser compiled by old seed, not compiler admission",
        "source_head": head, "source_tree": tree,
        "parser_sources": sources, "corpus_sources": corpus,
        "registry": byte_identity(registry_bytes), "harness": identity(harness),
        "implementation": [{"path": "controller/" + name, **byte_identity(data)}
                           for name, data in implementation.items()],
        "compile": "not_run", "parser_execution": "not_run", "typed_body": "not_implemented",
    }
    (output / "source-fence.json").write_text(json.dumps(fence, indent=2) + "\n")
    return fence


if __name__ == "__main__":
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("output", type=Path, help="new isolated directory, must not exist")
    args = arguments.parse_args()
    fence = prepare(args.output.resolve())
    print(json.dumps({"output": str(args.output.resolve()), "parser_files": len(fence["parser_sources"]),
                      "corpus_files": len(fence["corpus_sources"]), "compile": "not_run"}))
