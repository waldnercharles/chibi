#!/usr/bin/env python3
"""Inspect the proposal's text. Not a Chibi parser, compiler or interpreter."""
import hashlib
import json
from pathlib import Path
import re


def inventory(path):
    raw = path.read_bytes()
    source = re.sub(r"//[^\n]*", "", raw.decode("ascii"))
    functions = {}
    for name, parameters in (("ReadU32", ["input", "at"]), ("Census", ["input"])):
        start = source.index("{", source.index(name + " ::"))
        depth, end = 1, start + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        body = source[start + 1:end - 1]
        declaration = r"\b([A-Za-z_]\w*)\s*(?::=|:\s*(?:i64|bool)\s*=)"
        locals_ = re.findall(declaration, body)
        stack, peak = [len(parameters)], len(parameters)
        for match in re.finditer(r"\{|\}|" + declaration, body):
            if match[0] == "{":
                stack.append(0)
            elif match[0] == "}":
                stack.pop()
            else:
                stack[-1] += 1
                peak = max(peak, sum(stack))
        branches = len(re.findall(r"\bif\s*\(", body))
        alternatives = len(re.findall(r"\belse\b", body))
        loops = len(re.findall(r"\bwhile\s*\(", body))
        conjunctions = body.count("&&")
        functions[name] = {
            "parameters": parameters, "local_declarations": locals_,
            "binding_count": len(parameters) + len(locals_),
            "peak_lexically_visible_bindings": peak,
            "if_sites": branches, "else_sites": alternatives,
            "while_sites": loops, "short_circuit_and_sites": conjunctions,
            "projected_blocks": 1 + 2 * branches + alternatives + 3 * loops + 2 * conjunctions,
            "bindings": {
                binding: {
                    "body_token_occurrences": len(re.findall(r"\b" + binding + r"\b", body)),
                    "declarations_in_body": int(binding in locals_),
                    "assignment_writes": len(re.findall(r"\b" + binding + r"\s*=(?!=)", body)),
                    "read_uses": len(re.findall(r"\b" + binding + r"\b", body))
                    - int(binding in locals_)
                    - len(re.findall(r"\b" + binding + r"\s*=(?!=)", body)),
                } for binding in parameters + locals_},
        }
    return {
        "classification": "lexical_source_inventory_not_parser_or_native_evidence",
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "source_bytes": len(raw),
        "physical_lines": raw.count(b"\n") + int(bool(raw) and not raw.endswith(b"\n")),
        "nonblank_lines": sum(bool(line.strip(b" \t\v\f\r")) for line in raw.split(b"\n")),
        "functions": functions,
        "census_projected_additional_blocks_by_source_section": {
            "header_lines_61_72": 21,
            "record_validation_lines_74_104": 60,
            "payload_scan_and_tail_lines_105_127": 18,
            "category_commit_lines_128_151": 5,
            "final_validation_and_report_lines_152_179": 16,
            "entry_block": 1,
        },
        "reviewed_loop_write_sets": {
            "outer_i64": ["status", "error_at", "packages", "original_files", "original_bytes",
                          "original_lines", "original_nonblank", "replaced_files", "replaced_bytes",
                          "replaced_lines", "replaced_nonblank", "generated_files", "generated_bytes",
                          "generated_lines", "generated_nonblank", "pending_package", "at"],
            "inner_i64": ["file_lines", "file_nonblank", "last_byte", "cursor"],
            "inner_bool": ["line_has_text"],
        },
        "static_calls": {name: len(re.findall(r"\b" + name + r"\s*\(", source))
                         for name in ("ReadU32", "byte_length", "byte_read_checked", "chibi_census_report")},
        "new_feature_sites": {
            "BorrowedBytes_annotations": len(re.findall(r"\bBorrowedBytes\b", source)),
            "while_statements": len(re.findall(r"\bwhile\b", source)),
            "subtraction_operators": len(re.findall(r"(?<!-)-(?!>)", source)),
        },
    }


if __name__ == "__main__":
    print(json.dumps(inventory(Path(__file__).with_name("Main.chibi")), indent=2))
