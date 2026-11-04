#!/usr/bin/env python3
"""Snapshot the source-native correctness tests and an existing backend; never run tools."""

import argparse
import json
from pathlib import Path
import re
import runpy
import stat
import types

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
TYPED_PATH = HERE.parent / "typed/prepare.py"
typed = types.ModuleType("native_typed_preparation")
typed.__file__ = str(TYPED_PATH)
exec(compile(TYPED_PATH.read_bytes(), str(TYPED_PATH), "exec"), typed.__dict__)
helpers = typed.helpers
BRIDGE_ROOT = "tools/experiments/vnext/frontend/bridge"
CORE_FILES = ("os/Os.chibi", "bytes/Bytes.chibi", "bytes/Tests.chibi",
              "hash/sha256/Sha256.chibi", "hash/sha256/Tests.chibi",
              "sys/posix/Posix.chibi", "target/Target.chibi",
              "sys/windows/Decimal.chibi", "sys/windows/Windows.chibi")
SOURCE_MAP = [*typed.SOURCE_MAP,
              (BRIDGE_ROOT + "/generated/Pack.chibi", "project/vnext_bridge/Pack.chibi"),
              *(("packages/core/" + name, "core/" + name) for name in CORE_FILES)]
INPUT_MAP = [(BRIDGE_ROOT + "/tests/" + name, "inputs/" + name)
             for name in ("NativeCases.chibi", "NativeMain.chibi.in", "native_aot_caller.c")]


def prepare(output, backend_library):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"output must be a new directory: {output}")
    if output.resolve() != output:
        raise ValueError(f"output must not traverse symlinks or parent components: {output}")
    library = Path(backend_library).absolute()
    if not stat.S_ISREG(library.lstat().st_mode) or library.resolve() != library:
        raise ValueError(f"backend must be a regular non-symlink file: {library}")
    backend = {"path": str(library), "resolved": str(library), **helpers.identity(library)}
    if not backend["bytes"]:
        raise ValueError(f"backend is empty: {library}")
    if len(SOURCE_MAP) != 28 or len(INPUT_MAP) != 3:
        raise ValueError("source-native snapshot membership changed; inspect before expanding it")
    data, origins, graph = typed.read_sources(SOURCE_MAP, INPUT_MAP)
    pending = typed.imports(data["inputs/NativeMain.chibi.in"], "NativeMain.chibi.in")
    if pending != ["core:os", "project:vnext_bridge", "project:vnext_typed"]:
        raise ValueError("native test imports changed")
    typed.check_closure(graph, pending)

    # Reuse the generator's closed Model reader and both generated outputs.
    # No subprocess, compiler, build, or native-library loading happens here.
    generator_path = HERE / "generate.py"
    generator = runpy.run_path(str(generator_path))
    model = generator["parse_model"](data["project/vnext_typed/Model.chibi"].decode("utf-8"))
    for name, content in generator["generated"](model).items():
        path = HERE / "generated" / name
        if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path or path.read_bytes() != content.encode("utf-8"):
            raise ValueError(f"generated binding is stale or symlinked: {path}")

    template = data["inputs/NativeMain.chibi.in"]
    contexts = ((b'LIBRARY_PATH', rb'^foreign import backend "LIBRARY_PATH"$'),
                (b'FIXTURE_SOURCE', rb'^    source := FIXTURE_SOURCE;$'),
                (b'OUTPUT_PATH', rb'^    output_path := OUTPUT_PATH;$'))
    for marker, context in contexts:
        if template.count(marker) != 1 or len(re.findall(context, template, re.M)) != 1:
            raise ValueError(f"native template requires one complete {marker.decode()} substitution context")
    fixture = data["inputs/NativeCases.chibi"]
    source_literal = helpers.literal(fixture.decode("utf-8"))
    if json.loads(source_literal).encode("utf-8") != fixture:
        raise ValueError("native fixture literal does not preserve UTF-8 bytes")
    substitutions = {b'"LIBRARY_PATH"': helpers.literal(str(library)).encode("utf-8"),
                     b'FIXTURE_SOURCE': source_literal.encode("utf-8"),
                     b'OUTPUT_PATH': helpers.literal(str(output / "run/normal/source-native.o")).encode("utf-8")}
    # A single pass never interprets marker-like text inside inserted literals.
    data["entry/Main.chibi"] = re.sub(rb'"LIBRARY_PATH"|FIXTURE_SOURCE|OUTPUT_PATH',
                                      lambda match: substitutions[match[0]], template)
    implementation = {}
    for name, path in (("run_native.py", HERE / "run_native.py"),
                       ("owned_process.py", HERE.parent.parent / "owned_process.py")):
        if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path:
            raise ValueError(f"controller must be a regular non-symlink file: {path}")
        implementation["controller/" + name] = path.read_bytes()
    manifest = {
        "schema": 1,
        "purpose": "working-tree source snapshot for developer correctness reproduction; not backend or toolchain admission",
        "directory": str(output),
        "files": [{"path": name, **({"source": origins[name]} if name in origins else
                   {"generated_from": ["inputs/NativeMain.chibi.in", "inputs/NativeCases.chibi"]}),
                   **helpers.byte_identity(value)} for name, value in sorted(data.items())],
        "imports": graph,
        "import_scope": "literal imports including conditional Windows branches; snapshot membership is not a compiled-file census",
        "backend": backend,
        "implementation": [{"path": name, **helpers.byte_identity(value)}
                           for name, value in sorted(implementation.items())],
        "preparation": [{"source": str(path.relative_to(ROOT)), **helpers.identity(path)}
                        for path in (Path(__file__).resolve(), TYPED_PATH, typed.HELPER_PATH, generator_path)],
        "stages": {name: "not_run" for name in ("chibi_compile", "jit_normal", "perf_failure", "c_link", "aot_run")},
    }
    payload = (json.dumps(manifest, indent=2, allow_nan=False) + "\n").encode("utf-8")
    output.mkdir(parents=True, exist_ok=False)
    for name, value in [*sorted(data.items()), *sorted(implementation.items()), ("source-manifest.json", payload)]:
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
    parser.add_argument("--backend", required=True, type=Path, help="existing backend shared library; never built here")
    args = parser.parse_args()
    try:
        result = prepare(args.output, args.backend)
    except (OSError, ValueError) as error:
        parser.exit(1, f"native preparation: {error}\n")
    print(json.dumps({"output": result["directory"], "snapshot_files": len(result["files"]) - 1,
                      "stages": result["stages"]}))
