#!/usr/bin/env python3
"""Local-only Linux/Clang-18/Ninja Debug transport boundary attestation.

This does not build or run anything. Ninja is used only with `-t commands`.
All detailed evidence goes to an explicitly supplied private log. The caller
must first establish the clean pinned checkout and build it normally.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import traceback


class AuditError(Exception):
    pass


def require(value, message):
    if not value:
        raise AuditError(message)


def canonical(value, base=None, exists=True):
    """Absolute lexical identity, with no symlink in any path component."""
    value = Path(value)
    require(value.is_absolute() or base is not None, "relative path without base")
    raw = value if value.is_absolute() else Path(base) / value
    require(raw.is_absolute(), "base path is not absolute")
    # Inspect the spelling before collapsing '..': link/../file can resolve
    # somewhere entirely different from its lexical normalization.
    current = Path(raw.anchor)
    for part in raw.parts[1:]:
        current = current.parent if part == ".." else current / part
        require(not current.is_symlink(), f"symlink alias: {current}")
    path = Path(os.path.abspath(raw))
    require(not any(c in str(path) for c in "\0\r\n\t"), "control character in path")
    for component in [*reversed(path.parents), path]:
        require(not component.is_symlink(), f"symlink alias: {component}")
    if exists:
        require(path.exists(), f"missing path: {path}")
    return path


def regular(path):
    path = canonical(path)
    require(stat.S_ISREG(path.stat().st_mode), f"not a regular file: {path}")
    return path


def contained(path, root):
    return path == root or root in path.parents


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(regular(path).read_text(), object_pairs_hook=pairs)


HOST_ARCHIVES = frozenset("""
chibi_behavior_runtime chibi_native_prepared chibi_text_capabilities
chibi_array_capabilities chibi_value_document chibi_asset_json
chibi_language_syntax chibi_execution chibi_events chibi_native_capabilities
chibi_fmt chibi_types chibi_asset_values chibi_json chibi_support
chibi_capabilities chibi_ecs chibi_execution_sljit chibi_syntax_runtime
""".split())

HOST_SOURCES = {
    "chibi-compiler-host": ["src/toolchain/compiler_main.c"],
    "chibi_compiler_host": [f"src/toolchain/{name}.c" for name in
                            ("compiler_artifact", "compiler_host", "compiler_json", "compiler_syntax")],
    "chibi_support": [f"src/support/{name}.c" for name in ("arena", "file_io", "xalloc", "rcss_map")],
    "chibi_types": [f"src/types/{name}.c" for name in
                    ("type", "catalog", "array", "record", "copy", "trace", "union", "index", "value_format", "value_wire")],
    "chibi_asset_values": ["src/support/asset_value.c"],
    "chibi_asset_json": ["src/support/asset_json.c"],
    "chibi_json": ["third_party/yyjson/yyjson.c"],
    "chibi_fmt": ["third_party/fmt/fmt.c"],
    "chibi_value_document": ["src/types/value_document.c", "src/types/value_document_read.c"],
    "chibi_capabilities": [f"src/capabilities/{name}.c" for name in ("catalog", "catalog_snapshot", "definition")],
    "chibi_native_capabilities": [f"src/runtime/{name}.c" for name in
                                  ("native_capabilities", "native_definitions", "type_definitions")],
    "chibi_ecs": [f"src/ecs/{name}.c" for name in ("ecs", "relationship", "hierarchy", "query", "component_catalog")],
    "chibi_events": ["src/runtime/events.c"],
    "chibi_behavior_runtime": ["src/runtime/event_catalog.c"] +
        [f"src/runtime/behaviors/{name}.c" for name in ("source", "source_profile", "source_publication", "package", "module_set")],
    "chibi_native_prepared": ["src/runtime/native_prepared.c"],
    "chibi_array_capabilities": ["src/runtime/arrays.c", "src/runtime/arrays_definition.c"],
    "chibi_text_capabilities": ["src/runtime/text_format.c", "src/runtime/text_definition.c"],
    "chibi_language_syntax": ["src/language/syntax.c", "src/language/src/parser.c", "src/language/src/scanner.c"],
    "chibi_execution": [f"src/execution/{name}.c" for name in """
        artifact backend borrow c call component ecs_reader function function_builder fusion host inputs intrinsics
        jit jit_component jit_host_call jit_integer jit_memory jit_registers jit_suspend jit_system jit_watch log
        machine module module_effects opt parallel preparation_budget prepared prepared_function prepared_record
        program program_inspection program_activation program_observers program_bindings program_commands
        program_locals program_storage program_preflight program_tasks range_opt reads request
        runner scalar_lowering_plan scalarize source_debug state storage suspend time vm update_jit wasm_emit wasm_jit
    """.split()],
}

HOST_REQUIRED = frozenset("""
main compiler_host_load compiler_host_invoke compiler_host_invoke_bytes
compiler_host_invoke_batch compiler_artifact_read package_read_budget
execution_prepared_read_budget
""".split())
WRITER_REQUIRED = frozenset(("execution_prepared_write", "compiler_artifact_write"))
FORBIDDEN_SYMBOL = re.compile(r"^(?:authoring_packages_prepare|execution_prepared_.*write.*|semantic_.*|lower_.*)$")
ALLOWED_NEEDED = frozenset(("libc.so.6", "libm.so.6", "libgcc_s.so.1"))


def split_link(command):
    tokens = shlex.split(command)
    if tokens[:2] == [":", "&&"] and tokens[-2:] == ["&&", ":"]:
        tokens = tokens[2:-2]
    require(tokens and not any(token in {"&&", ";", "||", "|", ">", "<", "&"} for token in tokens),
            "unaccounted shell command in link recipe")
    return tokens


def classify_link(tokens, build, output, compiler, archives, objects, host):
    require(tokens and tokens[0] == compiler, "link compiler differs from configured compiler")
    require(tokens.count("-o") == 1, "link must have exactly one explicit output")
    position = tokens.index("-o")
    require(position + 1 < len(tokens), "missing link output")
    require(canonical(tokens[position + 1], build) == output, "wrong linked output identity")
    inputs, flags = [], []
    i = 1
    while i < len(tokens):
        token = tokens[i]
        if token == "-o":
            i += 2
            continue
        if token in ({"-g", "-Wl,--gc-sections", "-lm"} if host else {"-g", "-lm", "-ldl"}):
            flags.append(token)
        elif not token.startswith("-") and token.endswith((".o", ".a")):
            path = canonical(token, build)
            require(path in (objects if token.endswith(".o") else archives), f"unaccounted link input: {path}")
            inputs.append(path)
        else:
            raise AuditError(f"unaccounted linker token: {token}")
        i += 1
    require(Counter(path for path in inputs if path.suffix == ".o") == Counter(objects), "direct object inputs differ")
    require(set(path for path in inputs if path.suffix == ".a") == set(archives), "archive closure differs")
    require(flags.count("-g") == 1, "not normal Debug link")
    require(flags.count("-Wl,--gc-sections") == (1 if host else 0), "unexpected section-GC policy")
    return inputs, flags


def archive_members(data):
    """Parse normal GNU/BSD ar without invoking extraction or trusting names."""
    require(data.startswith(b"!<arch>\n"), "not a normal archive (thin archives are forbidden)")
    offset, strings, result = 8, b"", []
    while offset < len(data):
        require(offset + 60 <= len(data), "truncated archive header")
        header = data[offset:offset + 60]
        require(header[58:60] == b"`\n", "bad archive header")
        size = int(header[48:58].strip())
        require(size >= 0 and offset + 60 + size <= len(data), "bad archive member length")
        name = header[:16].decode("ascii").strip()
        payload = data[offset + 60:offset + 60 + size]
        offset += 60 + size + (size & 1)
        if name == "//":
            require(not strings, "duplicate archive long-name table")
            strings = payload
            continue
        if name in {"/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED"}:
            continue
        if name.startswith("#1/"):
            length = int(name[3:])
            require(0 < length <= len(payload), "invalid BSD archive name")
            name, payload = payload[:length].rstrip(b"\0").decode(), payload[length:]
        elif name.startswith("/"):
            index = int(name[1:])
            require(0 <= index < len(strings), "invalid GNU archive name")
            end = strings.find(b"/\n", index)
            require(end >= index, "unterminated GNU archive name")
            name = strings[index:end].decode()
        else:
            name = name.removesuffix("/")
        require(name and "/" not in name and "\\" not in name, "unsafe archive member name")
        require(payload[:4] == b"\x7fELF", "non-ELF archive member")
        result.append((name, digest(payload)))
    require(offset == len(data), "truncated archive padding")
    require(result, "empty archive")
    return Counter(result)


def parse_symbols(text):
    result = {}
    for line in text.splitlines():
        # nm -P --no-demangle: name type value size; archive headings end in ':'.
        if not line or line.endswith(":"):
            continue
        fields = line.split()
        require(len(fields) >= 2 and len(fields[1]) == 1, "unrecognized nm output")
        name, kind = fields[:2]
        result.setdefault(name.split("@")[0], set()).add(kind)
    return result


def defined(symbols):
    return {name for name, kinds in symbols.items() if kinds - {"U", "u", "w", "v"}}


def validate_elf_text(header, sections, dynamic, symbols, required, forbidden=()):
    require(re.search(r"Class:\s+ELF64\b", header), "not ELF64")
    require(re.search(r"Type:\s+(?:EXEC|DYN)\b", header), "not an ELF executable")
    require("Advanced Micro Devices X86-64" in header, "unsupported ELF machine")
    for section in (".symtab", ".strtab", ".debug_info", ".debug_line"):
        require(re.search(r"\s" + re.escape(section) + r"\s", sections), f"unstripped Debug section missing: {section}")
    require(".gnu_debuglink" not in sections, "split/stripped debug executable is not allowed")
    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic)
    require(needed and len(needed) == len(set(needed)), "missing or duplicate dynamic dependencies")
    require(set(needed) <= ALLOWED_NEEDED and "libc.so.6" in needed, f"unexpected actual dynamic dependencies: {needed}")
    require(not re.search(r"\((?:RPATH|RUNPATH|FILTER|AUXILIARY|AUDIT|DEPAUDIT|TEXTREL)\)", dynamic), "unexpected dynamic loader policy")
    require(required <= defined(symbols), f"required symbols absent: {sorted(required - defined(symbols))}")
    require(not (set(symbols) & set(forbidden)), f"forbidden symbols present: {sorted(set(symbols) & set(forbidden))}")
    return needed


class Runner:
    def __init__(self, log):
        self.log = log
        self.dwarf_checked = {}
        self.tools = {}
        for name in ("ninja", "nm", "readelf"):
            path = shutil.which(name)
            require(path, f"missing read-only tool: {name}")
            self.tools[name] = str(Path(path).resolve(strict=True))

    def run(self, tool, *args):
        command = [self.tools[tool], *map(str, args)]
        self.log.write("COMMAND " + json.dumps(command) + "\n")
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, timeout=30, env={**os.environ, "LC_ALL": "C"})
        self.log.write(result.stdout)
        self.log.write(result.stderr)
        require(result.returncode == 0, f"read-only tool failed: {tool}")
        return result.stdout

    def symbols(self, path, globals_only=False):
        return parse_symbols(self.run("nm", "-P", "--no-demangle", *( ["-g"] if globals_only else [] ), path))

    def elf(self, path, required, forbidden=()):
        require(regular(path).read_bytes()[:4] == b"\x7fELF", "not an ELF file")
        require(os.access(path, os.X_OK), "product file is not executable")
        program = self.run("readelf", "-lW", path)
        interpreters = re.findall(r"\[Requesting program interpreter: ([^\]]+)\]", program)
        require(interpreters == ["/lib64/ld-linux-x86-64.so.2"], "unexpected or missing ELF interpreter")
        symbols = self.symbols(path)
        needed = validate_elf_text(self.run("readelf", "-hW", path), self.run("readelf", "-SW", path),
                                   self.run("readelf", "-dW", path), symbols, required, forbidden)
        self.log.write("ELF " + json.dumps({"path": str(path), "sha256": digest(path.read_bytes()), "needed": needed}) + "\n")
        return symbols

    def dwarf_owner(self, obj, source):
        if obj in self.dwarf_checked:
            require(self.dwarf_checked[obj] == source, "object requested with conflicting DWARF owner")
            return
        output = self.run("readelf", "--debug-dump=info", "--dwarf-depth=1", obj)
        names = re.findall(r"DW_AT_name\s*:\s*(?:\([^\n]*?\):\s*)?([^\n]+)", output)
        directories = re.findall(r"DW_AT_comp_dir\s*:\s*(?:\([^\n]*?\):\s*)?([^\n]+)", output)
        require(len(names) == len(directories) == 1, "ambiguous or absent object DWARF owner")
        require(canonical(names[0].strip(), canonical(directories[0].strip())) == source,
                f"DWARF owner mismatch: {obj}")
        require(re.search(r"DW_AT_producer[^\n]*clang version 18\.", output), "object not produced by Clang 18")
        self.dwarf_checked[obj] = source


class Build:
    def __init__(self, root, build, suite, runner):
        self.root, self.path, self.suite, self.runner = root, canonical(build), suite, runner
        self.source = canonical(root / "test" / suite)
        reply = canonical(self.path / ".cmake/api/v1/reply")
        indexes = sorted(reply.glob("index-*.json"))
        require(len(indexes) == 1, "fresh build must contain exactly one File API index")
        index = load_json(indexes[0])
        require(index["cmake"]["generator"]["name"] == "Ninja", "not Ninja")
        require(not index["cmake"]["generator"].get("multiConfig", False), "multi-config unsupported")
        objects = {}
        for item in index["objects"]:
            key = (item["kind"], item["version"]["major"])
            require(key not in objects, "duplicate File API object")
            objects[key] = load_json(canonical(item["jsonFile"], reply))
        model = objects[("codemodel", 2)]
        require(canonical(model["paths"]["build"]) == self.path, "wrong codemodel build root")
        require(canonical(model["paths"]["source"]) == self.source, "wrong codemodel source root")
        require(len(model["configurations"]) == 1 and model["configurations"][0]["name"] == "Debug", "not single Debug configuration")
        self.cache = {item["name"]: item["value"] for item in objects[("cache", 2)]["entries"]}
        require(len(self.cache) == len(objects[("cache", 2)]["entries"]), "duplicate CMake cache entry")
        for key, value in {"CMAKE_BUILD_TYPE": "Debug", "CMAKE_GENERATOR": "Ninja", "CHIBI_SDK_BUILD_COMPILER": "ON",
                           "CHIBI_SDK_BUILD_NATIVE": "OFF", "CHIBI_SDK_BUILD_DESKTOP": "OFF", "CHIBI_ENABLE_SJLIT": "ON",
                           "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"}.items():
            require(self.cache.get(key) == value, f"unexpected {key}")
        for key, value in self.cache.items():
            if re.fullmatch(r"CMAKE_(?:C_FLAGS|EXE_LINKER_FLAGS|STATIC_LINKER_FLAGS)(?:_DEBUG)?", key):
                require(value == ("-g" if key == "CMAKE_C_FLAGS_DEBUG" else ""), f"custom build flags: {key}")
        self.compiler = self.cache["CMAKE_C_COMPILER"]
        require(Path(self.compiler).is_absolute() and Path(self.compiler).name == "clang-18", "not explicit clang-18")
        toolchains = objects[("toolchains", 1)]["toolchains"]
        ctools = [item for item in toolchains if item["language"] == "C"]
        require(len(ctools) == 1 and ctools[0]["compiler"]["id"] == "Clang" and
                ctools[0]["compiler"]["version"].startswith("18."), "wrong compiler identity")
        require(ctools[0]["compiler"]["path"] == self.compiler, "compiler metadata mismatch")
        self.targets, self.by_id = {}, {}
        for ref in model["configurations"][0]["targets"]:
            target = load_json(canonical(ref["jsonFile"], reply))
            require(target["name"] == ref["name"] and target["id"] == ref["id"], "target identity mismatch")
            require(target["name"] not in self.targets and target["id"] not in self.by_id, "duplicate target")
            self.targets[target["name"]] = target
            self.by_id[target["id"]] = target
        self.compiles = {}
        for entry in load_json(self.path / "compile_commands.json"):
            directory = canonical(entry["directory"])
            require(directory == self.path, "unexpected compile working directory")
            args = entry.get("arguments") or shlex.split(entry["command"])
            require(args.count("-o") == args.count("-c") == 1, "ambiguous compile input/output")
            obj = canonical(args[args.index("-o") + 1], directory, exists=False)
            source = canonical(entry["file"], directory, exists=False)
            require(canonical(args[args.index("-c") + 1], directory, exists=False) == source, "compile source mismatch")
            require(contained(obj, self.path), "compile output outside build")
            require(obj not in self.compiles, "multiple compilation owners for object")
            if "output" in entry:
                require(canonical(entry["output"], directory, exists=False) == obj, "compile output metadata mismatch")
            self.compiles[obj] = (source, args)
        self.owned = {}

    def artifact(self, target):
        metadata = self.targets[target]
        entries = metadata.get("artifacts", [])
        require(len(entries) == 1, f"ambiguous target artifact: {target}")
        path = canonical(entries[0]["path"], self.path)
        require(contained(path, self.path), "target artifact outside build")
        directory = canonical(metadata["paths"]["build"], self.path)
        require(contained(directory, self.path), "target owning directory outside build")
        if metadata["type"] == "STATIC_LIBRARY":
            require(path == directory / ("lib" + target + ".a"), "archive path differs from owning target's normal full output")
        if "nameOnDisk" in metadata:
            require(metadata["nameOnDisk"] == path.name, "target output name metadata mismatch")
        return regular(path)

    def objects(self, name):
        if name in self.owned:
            return self.owned[name]
        target = self.targets[name]
        prefix = canonical(Path(target["paths"]["build"]) / "CMakeFiles" / (name + ".dir"), self.path, exists=False)
        expected = [canonical(item["path"], self.source) for item in target.get("sources", []) if "compileGroupIndex" in item]
        require(len(expected) == len(set(expected)), f"duplicate source in target: {name}")
        candidates = {obj: pair[0] for obj, pair in self.compiles.items() if contained(obj, prefix)}
        require(Counter(candidates.values()) == Counter(expected), f"source-to-object ownership mismatch: {name}")
        require(candidates, f"target has no compilation owners: {name}")
        for obj in candidates:
            source, args = self.compiles[obj]
            regular(source)
            require(args and args[0] == self.compiler and source.suffix == ".c", "owned object does not use configured C compiler")
            require("-g" in args and not any(arg.startswith(("-O", "-flto", "-fsanitize", "-fprofile", "-fdebug-prefix-map", "-ffile-prefix-map"))
                                          or arg in {"-g0", "-gsplit-dwarf", "-s"} for arg in args), "non-normal Debug compilation")
        self.owned[name] = candidates
        return candidates

    def dependency_targets(self, name):
        seen, pending = set(), [name]
        while pending:
            current = pending.pop()
            for dep in self.targets[current].get("dependencies", []):
                target = self.by_id[dep["id"]]["name"]
                if target not in seen:
                    seen.add(target)
                    pending.append(target)
        return seen

    def linked_targets(self, name):
        """Use link fragments, not File API build/order dependencies, for archives."""
        fragments = []
        for fragment in self.targets[name]["link"]["commandFragments"]:
            require(fragment["role"] in {"flags", "libraries"}, "unaccounted File API linker role")
            fragments.extend(shlex.split(fragment["fragment"]))
        archive_paths = {canonical(token, self.path) for token in fragments
                         if not token.startswith("-") and token.endswith(".a")}
        dependencies = self.dependency_targets(name)
        archives = {}
        for dep in dependencies:
            if self.targets[dep]["type"] == "STATIC_LIBRARY":
                entries = self.targets[dep].get("artifacts", [])
                require(len(entries) == 1, "ambiguous static artifact")
                path = canonical(entries[0]["path"], self.path, exists=False)
                if path in archive_paths:
                    self.artifact(dep)
                    require(path not in archives.values(), "multiple targets own same archive")
                    archives[dep] = path
        require(set(archives.values()) == archive_paths, "File API links an archive without a target owner")
        return fragments, archives

    def direct_objects(self, name):
        objects = dict(self.objects(name))
        external = {canonical(item["path"], self.source) for item in self.targets[name].get("sources", [])
                    if "compileGroupIndex" not in item and item["path"].endswith(".o")}
        owners = set()
        for dep in self.dependency_targets(name):
            if self.targets[dep]["type"] != "OBJECT_LIBRARY":
                continue
            prefix = canonical(Path(self.targets[dep]["paths"]["build"]) / "CMakeFiles" / (dep + ".dir"), self.path, exists=False)
            if not any(contained(obj, prefix) for obj in external):
                continue
            owned = self.objects(dep)
            if external & set(owned):
                require(set(owned) <= external, "partial object-library input")
                owners.add(dep)
                require(not (set(objects) & set(owned)), "duplicate owning object")
                objects.update(owned)
        require(set(objects) - set(self.objects(name)) == external, "external object has no unique linked object-library owner")
        return objects, owners

    def verify_archive(self, name):
        artifact = self.artifact(name)
        require(artifact.suffix == ".a", "static target has non-archive output")
        objects = self.objects(name)
        expected = Counter((obj.name, digest(regular(obj).read_bytes())) for obj in objects)
        require(archive_members(artifact.read_bytes()) == expected, f"archive bytes do not match owning objects: {name}")
        self.runner.log.write("ARCHIVE " + json.dumps({"target": name, "path": str(artifact),
            "sha256": digest(artifact.read_bytes()), "objects": {str(obj): str(source) for obj, source in objects.items()}}) + "\n")

    def link(self, name, host=False):
        target = self.targets[name]
        require(target["type"] == "EXECUTABLE", "not executable target")
        require(target["link"]["language"] == "C" and not target["link"].get("lto") and not target["link"].get("sysroot"),
                "unexpected link language, LTO, or sysroot")
        output = self.artifact(name)
        expected_output = self.path / ("sdk/chibi-compiler-host" if host else name)
        require(output == expected_output, "non-product output location")
        fragments, archives = self.linked_targets(name)
        objects, object_targets = self.direct_objects(name)
        # add_dependencies on unrelated executables is order-only, not link input.
        if host:
            require(set(archives) == HOST_ARCHIVES, "host archive target closure differs from pinned runtime-only closure")
            require(object_targets == {"chibi_compiler_host"}, "host object target closure differs")
            for dep, path in archives.items():
                directory = "sdk/language" if dep == "chibi_language_syntax" else "sdk"
                require(path == self.path / directory / ("lib" + dep + ".a"), "host archive is not its exact product output")
        commands = self.runner.run("ninja", "-C", self.path, "-t", "commands", str(output.relative_to(self.path)))
        links = []
        for command in commands.splitlines():
            tokens = shlex.split(command)
            if "-o" in tokens:
                positions = [i for i, token in enumerate(tokens) if token == "-o"]
                if any(i + 1 < len(tokens) and canonical(tokens[i + 1], self.path, exists=False) == output for i in positions):
                    links.append(split_link(command))
        require(len(links) == 1, "missing or ambiguous actual product link recipe")
        inputs, flags = classify_link(links[0], self.path, output, self.compiler, set(archives.values()), set(objects), host)
        # CMake excludes direct object inputs and -o from its command fragments.
        actual = [token for i, token in enumerate(links[0][1:], 1)
                  if token != "-o" and (i == 0 or links[0][i - 1] != "-o") and
                  not (not token.startswith("-") and token.endswith(".o"))]
        require(actual == fragments, "actual link recipe differs from CMake File API fragments")
        for dep in archives:
            self.verify_archive(dep)
        self.runner.log.write("LINK " + json.dumps({"target": name, "output": str(output),
            "archives": {dep: str(path) for dep, path in archives.items()}, "objects": {str(obj): str(src) for obj, src in objects.items()},
            "flags": flags}) + "\n")
        return output, archives, objects

    def one_source(self, name, relative, required):
        source = canonical(self.root / relative)
        objects = [obj for obj, owner in self.objects(name).items() if owner == source]
        require(len(objects) == 1, f"missing unique owning object: {relative}")
        obj = objects[0]
        require(required <= defined(self.runner.symbols(obj)), f"owning object missing symbols: {relative}")
        self.runner.dwarf_owner(obj, source)
        return obj

    def host_source_contract(self):
        expected = {name: {canonical(self.root / path) for path in paths} for name, paths in HOST_SOURCES.items()}
        expected["chibi_execution_sljit"] = {canonical(self.path / "sdk/private/sljit/sljitLir.c")}
        tree_root = self.cache.get("FETCHCONTENT_SOURCE_DIR_CHIBI_LANGUAGE_TREE_SITTER")
        require(tree_root, "pinned Tree-sitter cache source must be supplied explicitly")
        expected["chibi_syntax_runtime"] = {canonical(Path(tree_root) / "lib/src/lib.c")}
        for name, sources in expected.items():
            require(set(self.objects(name).values()) == sources, f"host source inventory changed: {name}")
            for obj, source in self.objects(name).items():
                self.runner.dwarf_owner(obj, source)
            if name != "chibi_execution":
                for obj in self.objects(name):
                    args = self.compiles[obj][1]
                    require(not any(arg in {"-ffunction-sections", "-fdata-sections"} for arg in args),
                            "unexpected section splitting outside owning execution target")
        for obj in self.objects("chibi_execution"):
            args = self.compiles[obj][1]
            require("-ffunction-sections" in args and "-fdata-sections" in args, "execution object lacks normal section splitting")


def audit(root, language_build, tools_build, runner):
    root = canonical(root)
    require(canonical(language_build) != canonical(tools_build), "language and tools must use separate builds")
    language = Build(root, language_build, "language", runner)
    tools = Build(root, tools_build, "tools", runner)
    tools.host_source_contract()
    host, _, host_objects = tools.link("chibi-compiler-host", host=True)
    producer, _, producer_objects = language.link("test_language")
    tooling, _, tooling_objects = tools.link("test_tools")
    frontend = set()
    for name in ("chibi_language", "chibi_compiler", "chibi_authoring"):
        language.verify_archive(name)
        frontend |= defined(runner.symbols(language.artifact(name), globals_only=True))
    forbidden = frontend | {"authoring_packages_prepare", "execution_prepared_write"}
    host_symbols = runner.elf(host, HOST_REQUIRED, forbidden)
    require(not any(FORBIDDEN_SYMBOL.fullmatch(name) for name in host_symbols), "forbidden frontend/writer entrypoint in host")
    runner.elf(producer, WRITER_REQUIRED | {"test_language_compiler_artifact_write", "authoring_packages_prepare"})
    runner.elf(tooling, WRITER_REQUIRED | {"test_tool_compiler_artifact", "compiler_host_load"})
    for build in (language, tools):
        build.one_source("chibi_execution", "src/execution/prepared.c", {"execution_prepared_write", "execution_prepared_read_budget"})
    language.one_source("test_language", "test/language/compiler_artifact_chibi.c", {"test_language_compiler_artifact_write"})
    language.one_source("test_language", "src/toolchain/compiler_artifact.c", {"compiler_artifact_write", "compiler_artifact_read"})
    tools.one_source("test_tools", "test/tools/compiler_artifact.c", {"test_tool_compiler_artifact"})
    tools.one_source("chibi_compiler_host", "src/toolchain/compiler_host.c", {"compiler_host_load", "compiler_host_invoke_bytes", "compiler_host_invoke_batch"})
    tools.one_source("chibi_compiler_host", "src/toolchain/compiler_artifact.c", {"compiler_artifact_write", "compiler_artifact_read"})
    require(producer_objects != tooling_objects and not (set(producer_objects) & set(tooling_objects)), "producer/tooling build separation failed")
    runner.log.write("All product-link, ownership, archive-byte, actual-ELF and positive-writer checks passed.\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--language-build", required=True)
    parser.add_argument("--tools-build", required=True)
    parser.add_argument("--private-log", required=True)
    args = parser.parse_args()
    try:
        log_path = canonical(args.private_log, exists=False)
        canonical(log_path.parent)
        fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "w") as log:
            try:
                audit(args.source_root, args.language_build, args.tools_build, Runner(log))
            except Exception:
                traceback.print_exc(file=log)
                raise
    except Exception:
        print("TRANSPORT_PRODUCT_LINK_AUDIT_FAIL")
        return 1
    print("TRANSPORT_PRODUCT_LINK_AUDIT_PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
