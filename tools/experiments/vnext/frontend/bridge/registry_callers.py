#!/usr/bin/env python3
"""Generate bounded native test callers from JSON values, never parse Chibi."""

import json
import re

# This checkpoint selects these nine source failures. Package visibility and
# foreign linking stay outside its producer/backend path.
REJECTIONS = {
    "argument_type": ("type", "direct call argument type does not match signature"),
    "argument_count": ("type", "direct call argument count does not match signature"),
    "condition_type": ("type", "if condition requires bool"),
    "result_type": ("type", "return type does not match function result"),
    "missing_return": ("type", "function has a reachable exit without return"),
    "unknown_name": ("resolve", "unknown scalar name"),
    "unsupported_array": ("subset", "vNext scalar slice supports only ordinary i64 and bool types"),
    "literal_range": ("type", "decimal literal is outside i64 range"),
    "duplicate_parameter": ("resolve", "duplicate parameter name"),
}
ACCEPTED = ("scalars", "direct_calls")
DEFERRED = ("modules", "foreign", "private_import", "missing_symbol")
REGISTRY_PATH = "tools/experiments/vnext/slice_cases.json"


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
            or chosen["files"] != [f"slice_cases/{selected}/Main.chibi"]):
        raise ValueError("selected native registry case requires its one exact source path")
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
    if selected == "scalars":
        for row in rows:
            name = row["id"]
            if name not in REJECTIONS:
                continue
            category, _ = REJECTIONS[name]
            rejection = row.get("reject")
            if (set(row) != {"id", "files", "reject"}
                    or row["files"] != [f"slice_cases/rejected/{name}.chibi"]
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


def generate(template, chosen, signatures, rejected, sources):
    """Generate both callers in bounded linear passes; <=128 calls × <=32 args, <=64 KiB/source."""
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
    template = replace_region(template, "FIXTURE_CHECKS",
                              '            assert(length(module.functions) > 0, "registered source produced functions");\n')
    lines = ["CheckSourceRejections :: () {"]
    for index, row in enumerate(rejected):
        source = sources[row["files"][0]]
        anchor = row["reject"][2].encode("utf-8")
        if source.count(anchor) != 1:
            raise ValueError(f"registry diagnostic anchor must occur once: {row['id']}")
        first, last = source.index(anchor), source.index(anchor) + len(anchor)
        label = json.dumps(f"registry {row['id']}: {row['reject'][0]} / {row['reject'][1]}")
        lines += [f"    source{index} := {json.dumps(source.decode('utf-8'), ensure_ascii=False)};",
                  "    // Neither arm can reach Pack or the backend.",
                  f"    switch typed.Produce(source{index}, {100 + index}) {{",
                  f"        case .Produced(module): assert(false, {label});",
                  "        case .Rejected(diagnostic):",
                  f'            assert(diagnostic.message == {json.dumps(REJECTIONS[row["id"]][1])}, {label});',
                  f'            assert(diagnostic.document == {100 + index} && diagnostic.start >= 0 && diagnostic.end > diagnostic.start && diagnostic.end <= text_byte_length(source{index}), "registered rejection source bounds");',
                  f'            assert(diagnostic.start < {last} && diagnostic.end > {first}, "registered rejection location anchor");',
                  "    }"]
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
