#!/usr/bin/env python3
"""Snapshot the source-native correctness tests and an existing backend; never run tools."""

import argparse
import json
from pathlib import Path
import re
import stat
import sys
import types

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
TYPED_PATH = HERE.parent / "typed/prepare.py"
typed = types.ModuleType("native_typed_preparation")
typed.__file__ = str(TYPED_PATH)
TYPED_BYTES = TYPED_PATH.read_bytes()
exec(compile(TYPED_BYTES, str(TYPED_PATH), "exec"), typed.__dict__)
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



def load_source(path):
    """Execute one exact source byte string and retain it for the manifest."""
    if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path:
        raise ValueError(f"generator must be a regular non-symlink file: {path}")
    source = path.read_bytes()
    name = "native_preparation_" + path.stem
    module = types.ModuleType(name)
    module.__file__ = str(path)
    previous = sys.modules.get(name)
    # Dataclasses resolve postponed annotations through their defining module.
    sys.modules[name] = module
    try:
        exec(compile(source, str(path), "exec"), module.__dict__)
    finally:
        if previous is None:
            del sys.modules[name]
        else:
            sys.modules[name] = previous
    return module.__dict__, source


def source_array(files, sources, document):
    """Transport one ordered closure; no source syntax or package facts are read."""
    if not 1 <= len(files) <= 8 or len(set(files)) != len(files):
        raise ValueError("native source closure requires 1..8 distinct files")
    if sum(len(sources[name]) for name in files) > 65536:
        raise ValueError("native source closure exceeds 64 KiB")
    rows = []
    for index, name in enumerate(files):
        source = sources[name]
        if not source:
            raise ValueError("native source file must not be empty")
        literal = helpers.literal(source.decode("utf-8"))
        if json.loads(literal).encode("utf-8") != source:
            raise ValueError("native source literal does not preserve UTF-8 bytes")
        rows.append(f"        {{path: {helpers.literal(name)}, text: {literal}, document: {document + index}}}")
    return "[\n" + ",\n".join(rows) + "\n    ]"


def prepare(output, backend_library, case="nativecases", foreign_archive=None):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"output must be a new directory: {output}")
    if output.resolve() != output:
        raise ValueError(f"output must not traverse symlinks or parent components: {output}")
    library = Path(backend_library).absolute()
    # Current ForeignNatives accepts a plain string, not escaped paths.
    if any(character in '\"\\' or ord(character) < 32 or ord(character) == 127
           for character in str(library)):
        raise ValueError("backend requires a plain foreign library path")
    if not stat.S_ISREG(library.lstat().st_mode) or library.resolve() != library:
        raise ValueError(f"backend must be a regular non-symlink file: {library}")
    backend = {"path": str(library), "resolved": str(library), **helpers.identity(library)}
    if not backend["bytes"]:
        raise ValueError(f"backend is empty: {library}")
    fixture_archive = None
    if case == "foreign":
        if foreign_archive is None:
            raise ValueError("foreign selection requires an existing fixture archive")
        archive_path = Path(foreign_archive).absolute()
        # Current ForeignNatives accepts a plain string, not escaped paths.
        if any(character in '\"\\' or ord(character) < 32 or ord(character) == 127
               for character in str(archive_path)):
            raise ValueError("fixture archive requires a plain foreign library path")
        if not stat.S_ISREG(archive_path.lstat().st_mode) or archive_path.resolve() != archive_path:
            raise ValueError(f"fixture archive must be a regular non-symlink file: {archive_path}")
        fixture_archive = {"path": str(archive_path), "resolved": str(archive_path),
                           **helpers.identity(archive_path)}
        if not fixture_archive["bytes"]:
            raise ValueError("fixture archive must not be empty")
    elif foreign_archive is not None:
        raise ValueError("only the foreign selection may use a fixture archive")
    # The only expansion is package_graph/Graph.chibi, imported by Produce.
    # Its core:strings dependency was already in the exact package snapshot.
    if len(SOURCE_MAP) != 29 or len(INPUT_MAP) != 3:
        raise ValueError("source-native snapshot membership changed; inspect before expanding it")
    caller_path = HERE / "registry_callers.py"
    selected = None
    input_map = INPUT_MAP
    fixture_files = ["NativeCases.chibi"]
    rejected = []
    if case != "nativecases":
        callers, caller_bytes = load_source(caller_path)
        registry_map = [(callers["REGISTRY_PATH"], "inputs/slice_cases.json")]
        registry, _, _ = typed.read_sources([], registry_map)
        selected, signatures, rejected = callers["read_registry"](registry["inputs/slice_cases.json"], case)
        fixture_files = selected["files"]
        input_files = dict.fromkeys(name for row in [selected, *rejected] for name in row["files"])
        input_map = [(BRIDGE_ROOT + "/tests/NativeMain.chibi.in", "inputs/NativeMain.chibi.in"),
                     *registry_map, (BRIDGE_ROOT + "/registry_callers.py", "inputs/registry_callers.py"),
                     *(("tools/experiments/vnext/" + name, "inputs/" + name) for name in input_files)]
    if case == "foreign":
        input_map = [*input_map,
                     ("tools/experiments/vnext/slice_probe.c", "inputs/slice_probe.c"),
                     (BRIDGE_ROOT + "/tests/foreign_bindings.c", "inputs/foreign_bindings.c"),
                     ("tools/experiments/vnext/slice_backend/foreign.h", "inputs/foreign.h")]
    data, origins, graph = typed.read_sources(SOURCE_MAP, input_map)
    if selected is not None:
        if data["inputs/slice_cases.json"] != registry["inputs/slice_cases.json"]:
            raise ValueError("registry changed while preparing native callers")
        if data["inputs/registry_callers.py"] != caller_bytes:
            raise ValueError("registry caller generator changed while preparing native callers")
    pending = typed.imports(data["inputs/NativeMain.chibi.in"], "NativeMain.chibi.in")
    if pending != ["core:os", "project:vnext_bridge", "project:vnext_typed"]:
        raise ValueError("native test imports changed")
    typed.check_closure(graph, pending)

    # Reuse the generator's closed Model reader and both generated outputs.
    # No subprocess, compiler, build, or native-library loading happens here.
    generator_path = HERE / "generate.py"
    generator, generator_bytes = load_source(generator_path)
    model = generator["parse_model"](data["project/vnext_typed/Model.chibi"].decode("utf-8"))
    for name, content in generator["generated"](model).items():
        path = HERE / "generated" / name
        if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path or path.read_bytes() != content.encode("utf-8"):
            raise ValueError(f"generated binding is stale or symlinked: {path}")

    template = data["inputs/NativeMain.chibi.in"]
    contexts = ((b'LIBRARY_PATH', rb'^foreign import backend "LIBRARY_PATH"$'),
                (b'FIXTURE_SOURCES', rb'^    sources: typed.Source\[\] = FIXTURE_SOURCES;$'),
                (b'OUTPUT_PATH', rb'^    output_path := OUTPUT_PATH;$'))
    for marker, context in contexts:
        if template.count(marker) != 1 or len(re.findall(context, template, re.M)) != 1:
            raise ValueError(f"native template requires one complete {marker.decode()} substitution context")
    source_bytes = {name: data["inputs/" + name]
                    for files in [fixture_files, *[row["files"] for row in rejected]] for name in files}
    source_literal = source_array(fixture_files, source_bytes, 19)
    substitutions = {b'"LIBRARY_PATH"': helpers.literal(str(library)).encode("utf-8"),
                     b'FIXTURE_SOURCES': source_literal.encode("utf-8"),
                     b'OUTPUT_PATH': helpers.literal(str(output / "run/normal/source-native.o")).encode("utf-8")}
    # A single pass never interprets marker-like text inside inserted literals.
    data["entry/Main.chibi"] = re.sub(rb'"LIBRARY_PATH"|FIXTURE_SOURCES|OUTPUT_PATH',
                                      lambda match: substitutions[match[0]], template)
    generated_from = ["inputs/NativeMain.chibi.in", *["inputs/" + name for name in fixture_files]]
    expectations = {"case": "nativecases", "rejections": [], "aot_checks": 45,
                    "aot_success": '{"aot_c_caller_checks":45,"status":"passed","input":"source-produced-copied-object"}\n'}
    if selected is not None:
        rejection_sources = []
        document = 100
        for row in rejected:
            rejection_sources.append((source_array(row["files"], source_bytes, document), document))
            document += len(row["files"])
        entry, c = callers["generate"](data["entry/Main.chibi"].decode("utf-8"), selected,
                                        signatures, rejected, source_bytes, rejection_sources,
                                        fixture_archive["path"] if fixture_archive is not None else None)
        data["entry/Main.chibi"] = entry
        data["inputs/native_aot_caller.c"] = c
        generated_from = ["inputs/NativeMain.chibi.in", "inputs/slice_cases.json",
                          "inputs/registry_callers.py", *["inputs/" + name for name in source_bytes]]
        expectations = callers["metadata"](selected, rejected)
    implementation = {}
    for name, path in (("run_native.py", HERE / "run_native.py"),
                       ("owned_process.py", HERE.parent.parent / "owned_process.py")):
        if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path:
            raise ValueError(f"controller must be a regular non-symlink file: {path}")
        implementation["controller/" + name] = path.read_bytes()
    preparation = [(Path(__file__).resolve(), Path(__file__).read_bytes()),
                   (TYPED_PATH, TYPED_BYTES), (typed.HELPER_PATH, typed.HELPER_BYTES),
                   (generator_path, generator_bytes)]
    if selected is not None:
        preparation.append((caller_path, caller_bytes))
    for path, source in preparation:
        if not stat.S_ISREG(path.lstat().st_mode) or path.resolve() != path or path.read_bytes() != source:
            raise ValueError(f"preparation source changed while generating native callers: {path}")
    manifest = {
        "schema": 2,
        "expectations": expectations,
        "purpose": "working-tree source snapshot for developer correctness reproduction; not backend or toolchain admission",
        "directory": str(output),
        "files": [{"path": name, **({"source": origins[name]} if name in origins else
                   {"generated_from": generated_from}),
                   **helpers.byte_identity(value)} for name, value in sorted(data.items())],
        "imports": graph,
        "import_scope": "literal imports including conditional Windows branches; snapshot membership is not a compiled-file census",
        "backend": backend,
        "implementation": [{"path": name, **helpers.byte_identity(value)}
                           for name, value in sorted(implementation.items())],
        "preparation": [{"source": str(path.relative_to(ROOT)), **helpers.byte_identity(source)}
                        for path, source in preparation],
        "stages": {name: "not_run" for name in ("chibi_compile", "jit_normal", "perf_failure", "c_link", "aot_run")},
    }
    if fixture_archive is not None:
        # The same exact archive is named in the Chibi import and later C link.
        # This binds bytes only; actual archive/code admission is separate.
        if helpers.identity(archive_path) != {key: fixture_archive[key] for key in ("bytes", "sha256")}:
            raise ValueError("fixture archive changed during preparation")
        manifest["foreign_archive"] = fixture_archive
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
    callers, _ = load_source(HERE / "registry_callers.py")
    parser.add_argument("--case", choices=("nativecases", *callers["ACCEPTED"]), default="nativecases",
                        help="one separate native module; default preserves the historical regression")
    parser.add_argument("--foreign-archive", type=Path,
                        help="existing admitted C probe plus binding archive; required only for foreign")
    args = parser.parse_args()
    try:
        result = prepare(args.output, args.backend, args.case, args.foreign_archive)
    except (OSError, ValueError) as error:
        parser.exit(1, f"native preparation: {error}\n")
    print(json.dumps({"output": result["directory"], "snapshot_files": len(result["files"]) - 1,
                      "stages": result["stages"]}))
