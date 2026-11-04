#!/usr/bin/env python3
"""Generate bounded native test callers from JSON values, never parse Chibi."""

import json
import re

# The selection owning each rejection is explicit; source-file count does not
# decide which diagnostic checks run. Foreign linking remains deferred.
REJECTIONS = {
    "argument_type": ("scalars", "type", "direct call argument type does not match signature"),
    "argument_count": ("scalars", "type", "direct call argument count does not match signature"),
    "condition_type": ("scalars", "type", "if condition requires bool"),
    "result_type": ("scalars", "type", "return type does not match function result"),
    "missing_return": ("scalars", "type", "function has a reachable exit without return"),
    "unknown_name": ("scalars", "resolve", "unknown scalar name"),
    "unsupported_array": ("scalars", "subset", "vNext scalar slice supports only ordinary i64 and bool types"),
    "literal_range": ("scalars", "type", "decimal literal is outside i64 range"),
    "duplicate_parameter": ("scalars", "resolve", "duplicate parameter name"),
    "private_import": ("modules", "resolve", "private member"),
}
ACCEPTED = ("scalars", "direct_calls", "modules")
DEFERRED = ("foreign", "missing_symbol")
REGISTRY_PATH = "tools/experiments/vnext/slice_cases.json"


def case_files(name):
    """Exact original corpus membership; never discover files from imports."""
    files = ([f"slice_cases/{name}/Main.chibi"] if name in ACCEPTED
             else [f"slice_cases/rejected/{name}.chibi"])
    if name in ("modules", "private_import"):
        files.append("slice_cases/modules/math/Math.chibi")
    return files


def scalar_type(value):
    if type(value) is bool:
        return "bool"
    if type(value) is int and -(1 << 63) <= value < (1 << 63):
        return "i64"
    raise ValueError("caller expectations require exact JSON bool or in-range i64 values")


def read_registry(raw, selected):
    if selected not in ACCEPTED:
        raise ValueError(f"unknown native registry selection: {selected}")
    if len(raw) > 65536:
        raise ValueError("native test registry exceeds 64 KiB")
    try:
        registry = json.loads(raw)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ValueError(f"invalid native test registry: {error}") from error
    if (not isinstance(registry, dict) or type(registry.get("version")) is not int
            or registry["version"] != 1 or registry.get("project_root") != "."):
        raise ValueError("native test registry requires version 1 and project_root '.'")
    rows = registry.get("cases")
    members = set(ACCEPTED) | set(REJECTIONS) | set(DEFERRED)
    if not isinstance(rows, list) or len(rows) != len(members):
        raise ValueError("native registry case membership changed")
    cases = {}
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            raise ValueError("native registry case requires an identifier")
        name = row["id"]
        if name not in members or name in cases:
            raise ValueError(f"unknown or duplicate native registry case: {name}")
        cases[name] = row
    if set(cases) != members:
        raise ValueError("native registry is missing a selected case")
    chosen = cases[selected]
    if (set(chosen) != {"id", "files", "calls"}
            or chosen["files"] != case_files(selected)):
        raise ValueError("selected native registry case requires its exact ordered source paths")
    calls = chosen["calls"]
    if not isinstance(calls, list) or not 1 <= len(calls) <= 128:
        raise ValueError("native registry requires 1..128 calls")
    signatures = {}
    for call in calls:
        if not isinstance(call, list) or len(call) != 3:
            raise ValueError("native registry call requires name, arguments, result")
        name, arguments, result = call
        # Safe shared Chibi/C identifiers. Reject reserved/internal spellings so
        # JSON cannot inject declarations or collide with either generated main.
        if not isinstance(name, str) or re.fullmatch(r"[A-Z][A-Za-z0-9]{0,63}", name) is None:
            raise ValueError("native registry export requires a bounded uppercase identifier")
        if not isinstance(arguments, list) or len(arguments) > 32:
            raise ValueError("native registry requires at most 32 positional arguments")
        signature = (tuple(map(scalar_type, arguments)), scalar_type(result))
        if name in signatures and signatures[name] != signature:
            raise ValueError(f"inconsistent expected native signature: {name}")
        signatures[name] = signature
    if len(signatures) > 32:
        raise ValueError("native registry exceeds 32 tested exports")
    rejected = []
    for row in rows:
        name = row["id"]
        if name not in REJECTIONS or REJECTIONS[name][0] != selected:
            continue
        _, category, _ = REJECTIONS[name]
        rejection = row.get("reject")
        if (set(row) != {"id", "files", "reject"}
                or row["files"] != case_files(name)
                or not isinstance(rejection, list) or len(rejection) != 3
                or rejection[0] != category
                or any(not isinstance(value, str) or not value for value in rejection)):
            raise ValueError(f"unsupported registry rejection shape: {name}")
        rejected.append(row)
    return chosen, signatures, rejected


def literal(value, language):
    kind = scalar_type(value)
    if kind == "bool":
        return "true" if value else "false"
    if language == "chibi":
        if value == -(1 << 63):
            return "-9223372036854775807 - 1"
        return str(value)
    if value == -(1 << 63):
        return "INT64_MIN"
    return f"(-INT64_C({-value}))" if value < 0 else f"INT64_C({value})"


def aot_success(count):
    return json.dumps({"aot_c_caller_checks": count, "status": "passed",
                       "input": "source-produced-copied-object"}, separators=(",", ":")) + "\n"


def metadata(chosen, rejected):
    return {"case": chosen["id"], "rejections": [row["id"] for row in rejected],
            "aot_checks": len(chosen["calls"]), "aot_success": aot_success(len(chosen["calls"]))}


def replace_region(template, region, content):
    begin, end = f"// BEGIN {region}\n", f"// END {region}\n"
    if template.count(begin) != 1 or template.count(end) != 1:
        raise ValueError(f"native template requires one complete {region} region")
    first, last = template.index(begin), template.index(end)
    if first >= last:
        raise ValueError(f"native template has reversed {region} markers")
    return template[:first] + content + template[last + len(end):]


def generate(template, chosen, signatures, rejected, sources, rejection_sources):
    """Generate callers from JSON values and prepared literals, never source syntax."""
    lines = ["CheckCalls :: (module: typed.Module, owner: rawptr) -> int {"]
    # Annotate before evaluation, matching the historical caller's full-width
    # literals. Never route a JSON integer through Chibi's default int type.
    constants = {}
    for _, arguments, result in chosen["calls"]:
        for value in [*arguments, result]:
            if type(value) is int and value not in constants:
                name = f"constant{len(constants)}"
                constants[value] = name
                lines.append(f"    {name}: i64 = {literal(value, 'chibi')};")
    indexes = {}
    for index, (name, (parameters, result)) in enumerate(signatures.items()):
        indexes[name] = index
        types = ", ".join("typed.Bool" if kind == "bool" else "typed.I64" for kind in parameters)
        result_type = "typed.Bool" if result == "bool" else "typed.I64"
        lines += [f'    address{index} := ExpectedEntry(module, owner, "{name}", [{types}], {result_type});',
                  f"    if (address{index} == 0) {{ return {index + 1}; }}"]
    lines += ["    // All expected signatures matched before the first callable cast.",
              "    for (function, index in module.functions) {",
              "        if (function.exported == 0) {",
              "            private_address := chibi_vnext_entry(owner, u32(index + 1));",
              '            assert(private_address == 0, "private function has no published entry");',
              "        }", "    }"]
    if chosen["id"] == "modules":
        lines += ['    for (name in ["Double", "Hidden", ".vnext.2", ".vnext.3"]) {',
                  '        private_entry := ExpectedEntry(module, owner, name, [typed.I64], typed.I64);',
                  '        assert(private_entry == 0, "dependency names cannot publish native entries");',
                  '    }']
    first = next(iter(signatures))
    parameters, result = signatures[first]
    wrong_result = "typed.I64" if result == "bool" else "typed.Bool"
    expected_parameters = ", ".join("typed.Bool" if kind == "bool" else "typed.I64" for kind in parameters)
    lines += [f'    wrong := ExpectedEntry(module, owner, "{first}", [{expected_parameters}], {wrong_result});',
              '    assert(wrong == 0, "wrong result type rejected before casting");',
              '    missing := chibi_vnext_entry(owner, u32(0));',
              '    past := chibi_vnext_entry(owner, u32(length(module.functions) + 1));',
              '    assert(missing == 0 && past == 0, "absent entries cannot be called");']
    for name, (parameters, result) in signatures.items():
        index = indexes[name]
        args = ", ".join(f"a{at}: {kind}" for at, kind in enumerate(parameters))
        lines.append(f"    call{index}: fn({args}) -> {result} = address{index};")
    for at, (name, arguments, result) in enumerate(chosen["calls"]):
        args = ", ".join(constants[value] if type(value) is int else literal(value, "chibi") for value in arguments)
        expected = constants[result] if type(result) is int else literal(result, "chibi")
        lines += [f"    result{at} := call{indexes[name]}({args});",
                  f'    assert(result{at} == {expected}, "registry {chosen["id"]} call {at + 1}: {name}");']
    lines += ["    return 0;", "}", ""]
    template = replace_region(template, "FIXTURE_CALLS", "\n".join(lines))
    checks = '            assert(length(module.functions) > 0, "registered source produced functions");\n'
    if chosen["id"] == "modules":
        checks += '''            assert(length(module.functions) == 3, "all original package bodies were produced");
            assert(FindFunction(module, "Apply") == 1 && FindFunction(module, ".vnext.2") == 2 && FindFunction(module, ".vnext.3") == 3, "original package names retain global function IDs");
            assert(module.functions[0].exported == 1 && module.functions[1].exported == 0 && module.functions[2].exported == 0, "only entry-package Apply is exported");
            for (function, index in module.functions) {
                origin := module.origins[int(function.origin) - 1];
                expected: u32 = 20;
                if (index == 0) { expected = 19; }
                assert(origin.document == expected, "function origin retains its original document");
            }
            for (origin in module.origins) {
                source := sources[0].text;
                if (origin.document == 20) { source = sources[1].text; }
                assert((origin.document == 19 || origin.document == 20) && origin.start < origin.end && origin.end <= u32(text_byte_length(source)), "origin stays inside its original source bytes");
            }
'''
    template = replace_region(template, "FIXTURE_CHECKS", checks)
    lines = ["CheckSourceRejections :: () {"]
    assert len(rejection_sources) == len(rejected), "one prepared source closure per rejection"
    for index, (row, (expression, document)) in enumerate(zip(rejected, rejection_sources)):
        source = sources[row["files"][0]]
        anchor = row["reject"][2].encode("utf-8")
        if source.count(anchor) != 1:
            raise ValueError(f"registry diagnostic anchor must occur once: {row['id']}")
        first, last = source.index(anchor), source.index(anchor) + len(anchor)
        label = json.dumps(f"registry {row['id']}: {row['reject'][0]} / {row['reject'][1]}")
        lines += [f"    sources{index}: typed.Source[] = {expression};",
                  "    // Neither arm can reach Pack or the backend.",
                  f"    switch typed.ProduceSources(sources{index}) {{",
                  f"        case .Produced(module): assert(false, {label});",
                  "        case .Rejected(diagnostic):",
                  f'            assert(diagnostic.message == {json.dumps(REJECTIONS[row["id"]][2])}, {label});',
                  f'            assert(diagnostic.document == {document} && diagnostic.start >= 0 && diagnostic.end > diagnostic.start && diagnostic.end <= text_byte_length(sources{index}[0].text), "registered rejection source bounds");',
                  f'            assert(diagnostic.start < {last} && diagnostic.end > {first}, "registered rejection location anchor");']
        if row["id"] == "private_import":
            line = source[:first].count(b"\n") + 1
            column = first - source.rfind(b"\n", 0, first)
            lines.append(f'            assert(diagnostic.start == {first} && diagnostic.end == {last} && diagnostic.line == {line} && diagnostic.column == {column}, "private member reports its exact original byte span and coordinates");')
        lines.append("    }")
    lines += ["}", ""]
    template = replace_region(template, "FIXTURE_REJECTIONS", "\n".join(lines))
    c = ["/* Generated only from exact JSON bool/int test values; no source interpretation. */",
         "#include <stdbool.h>", "#include <stdint.h>", "#include <stdio.h>", ""]
    c_types = {"bool": "bool", "i64": "int64_t"}
    for name, (parameters, result) in signatures.items():
        arguments = ", ".join(c_types[kind] for kind in parameters) or "void"
        c.append(f"extern {c_types[result]} {name}({arguments});")
    c += ["", "int main(void) {", "    unsigned checks = 0;"]
    for index, (name, arguments, result) in enumerate(chosen["calls"]):
        args = ", ".join(literal(value, "c") for value in arguments)
        c += [f"    if ({name}({args}) != {literal(result, 'c')}) {{",
              f'        fputs("registry {chosen["id"]} call {index + 1}: {name} failed\\n", stderr); return 1;',
              "    }", "    ++checks;"]
    c += ['    printf("{\\"aot_c_caller_checks\\":%u,\\"status\\":\\"passed\\",\\"input\\":\\"source-produced-copied-object\\"}\\n", checks);',
          "    return 0;", "}", ""]
    return template.encode("utf-8"), "\n".join(c).encode("utf-8")
