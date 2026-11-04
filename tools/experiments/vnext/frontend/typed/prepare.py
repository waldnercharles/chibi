#!/usr/bin/env python3
"""Copy the bounded typed-transform fixture and its source closure; never compile."""

import argparse
import json
from pathlib import Path
import re
import stat
import types

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
# Use the existing parser list and literal/hash operations, without importing a
# same-named module, consulting bytecode caches, or widening the module path.
HELPER_PATH = HERE.parent / "prepare.py"
HELPER_BYTES = HELPER_PATH.read_bytes()
helpers = types.ModuleType("retained_parser_preparation")
helpers.__file__ = str(HELPER_PATH)
exec(compile(HELPER_BYTES, str(HELPER_PATH), "exec"), helpers.__dict__)

COMPILER_FILES = (*helpers.PARSER_FILES,
    "json_string/JsonString.chibi", "lexical_binding/Binding.chibi",
    "named_binding/Binding.chibi", "signature_reader/Reader.chibi",
    "type_constructors/Constructors.chibi", "unit_algebra/Units.chibi",
    "package_graph/Graph.chibi")
TYPED_FILES = ("Model.chibi", "Produce.chibi", "Verify.chibi")
TYPED_ROOT = "tools/experiments/vnext/frontend/typed"
SOURCE_MAP = [("tools/compiler/" + name, "project/" + name) for name in COMPILER_FILES]
SOURCE_MAP += [("packages/core/strings/Strings.chibi", "core/strings/Strings.chibi")]
SOURCE_MAP += [(TYPED_ROOT + "/" + name, "project/vnext_typed/" + name) for name in TYPED_FILES]
FIXTURES = (
    (b"FIXTURE_SOURCE", TYPED_ROOT + "/fixtures/EchoJoin.chibi", "inputs/EchoJoin.chibi"),
    (b"PACKAGE_MAIN_SOURCE", "tools/experiments/vnext/slice_cases/modules/Main.chibi", "inputs/modules/Main.chibi"),
    (b"PACKAGE_MATH_SOURCE", "tools/experiments/vnext/slice_cases/modules/math/Math.chibi", "inputs/modules/math/Math.chibi"),
    (b"FOREIGN_SOURCE", "tools/experiments/vnext/slice_cases/foreign/Main.chibi", "inputs/foreign/Main.chibi"),
    (b"MISSING_SYMBOL_SOURCE", "tools/experiments/vnext/slice_cases/rejected/missing_symbol.chibi", "inputs/missing_symbol.chibi"),
    (b"PRIVATE_IMPORT_SOURCE", "tools/experiments/vnext/slice_cases/rejected/private_import.chibi", "inputs/private_import.chibi"),
)
INPUT_MAP = [(TYPED_ROOT + "/tests/Main.chibi.in", "inputs/Main.chibi.in"),
             *((source, copied) for _, source, copied in FIXTURES)]
PACKAGE_PART = r"[a-z_][a-z_0-9]{0,63}"
PACKAGE_PATH = rf"{PACKAGE_PART}:{PACKAGE_PART}(?:/{PACKAGE_PART}){{0,7}}"


def imports(data, name):
    result = set()
    for line in data.decode("utf-8").splitlines():
        if re.match(r"^\s*import\b", line):
            match = re.fullmatch(rf'\s*import\s+[A-Za-z_][A-Za-z_0-9]*\s+"({PACKAGE_PATH})"\s*', line)
            if match is None:
                raise ValueError(f"unsupported import declaration in {name}: {line}")
            result.add(match[1])
    return sorted(result)


def read_sources(source_map, input_map):
    """Read an explicit snapshot and its literal imports, including when branches."""
    # Inspect all inputs before reserving output. Copy only immediate package
    # files, never the neighboring test/fixture trees or a discovered dependency.
    packages = {}
    for original, copied in source_map + input_map:
        for name in (original, copied):
            if Path(name).is_absolute() or any(part in ("", ".", "..") for part in name.split("/")):
                raise ValueError(f"source map requires relative paths without traversal: {name}")
    if len({copied for _, copied in source_map + input_map}) != len(source_map + input_map):
        raise ValueError("source map contains duplicate destinations")
    for original, copied in source_map:
        packages.setdefault(str(Path(original).parent), []).append((original, copied))
    for package, files in packages.items():
        actual = sorted(str(path.relative_to(ROOT)) for path in (ROOT / package).glob("*.chibi"))
        if actual != sorted(original for original, _ in files):
            raise ValueError(f"source package membership changed: {package}")
    data, origins = {}, {}
    for original, copied in source_map + input_map:
        source = ROOT / original
        if not stat.S_ISREG(source.lstat().st_mode) or source.resolve() != source:
            raise ValueError(f"source must be a regular non-symlink file: {original}")
        data[copied] = source.read_bytes()
        origins[copied] = original
    graph = {}
    for files in packages.values():
        package_path = Path(files[0][1]).parent
        package = str(package_path).replace("/", ":", 1)
        dependencies = set()
        for _, copied in files:
            declarations = re.findall(r"^package\s+([A-Za-z_][A-Za-z_0-9]*)\s*$", data[copied].decode("utf-8"), re.M)
            if declarations != [package_path.name]:
                raise ValueError(f"package declaration mismatch: {copied}")
            dependencies.update(imports(data[copied], copied))
        graph[package] = sorted(dependencies)
    return data, origins, graph


def check_closure(graph, pending):
    pending = list(pending)
    reached = set()
    while pending:
        package = pending.pop()
        if package in reached:
            continue
        if package not in graph:
            raise ValueError(f"dependency outside the copied closure: {package}")
        reached.add(package)
        pending.extend(graph[package])
    if reached != set(graph):
        raise ValueError("copied closure contains an unreachable package")


def prepare(output):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"output must be a new directory: {output}")
    if len(COMPILER_FILES) != 15 or len(set(COMPILER_FILES)) != 15:
        raise ValueError("compiler file closure changed; inspect before expanding it")
    data, origins, graph = read_sources(SOURCE_MAP, INPUT_MAP)
    pending = imports(data["inputs/Main.chibi.in"], "tests/Main.chibi.in")
    if pending != ["core:strings", "project:vnext_typed"]:
        raise ValueError("test imports changed")
    check_closure(graph, pending)
    template = data["inputs/Main.chibi.in"]
    substitutions = {}
    for marker, _, copied in FIXTURES:
        if template.count(marker) != 1:
            raise ValueError(f"template must contain exactly one {marker.decode()} marker")
        fixture = data[copied]
        literal = helpers.literal(fixture.decode("utf-8"))
        if json.loads(literal).encode("utf-8") != fixture:
            raise ValueError(f"fixture literal does not preserve UTF-8 bytes: {copied}")
        substitutions[marker] = literal.encode("utf-8")
    # One pass never reinterprets marker-like text in an inserted source. Each
    # original file stays a separate string; no imports or bodies are parsed here.
    data["Main.chibi"] = re.sub(b"|".join(map(re.escape, substitutions)),
                               lambda match: substitutions[match[0]], template)
    manifest = {
        "schema": 1,
        "purpose": "copied working-tree source preparation only; not compiler or toolchain admission",
        "files": [{"path": name, **({"source": origins[name]} if name in origins else
                   {"generated_from": ["inputs/Main.chibi.in", *[copied for _, _, copied in FIXTURES]]}),
                   **helpers.byte_identity(value)} for name, value in sorted(data.items())],
        "imports": graph,
        "preparation": [{"source": str(path.relative_to(ROOT)), **helpers.byte_identity(value)}
                        for path, value in ((Path(__file__).resolve(), Path(__file__).read_bytes()),
                                            (HELPER_PATH, HELPER_BYTES))],
        "compile": "not_run", "typed_tests": "not_run",
    }
    payload = (json.dumps(manifest, indent=2, allow_nan=False) + "\n").encode("utf-8")
    output.mkdir(parents=True, exist_ok=False)
    for name, value in [*sorted(data.items()), ("source-manifest.json", payload)]:
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream:
            stream.write(value)
        if destination.read_bytes() != value:
            raise OSError(f"copied bytes changed: {name}")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="fresh output directory; must not already exist")
    args = parser.parse_args()
    result = prepare(args.output)
    print(json.dumps({"files": len(result["files"]), "compile": result["compile"],
                      "typed_tests": result["typed_tests"]}))
