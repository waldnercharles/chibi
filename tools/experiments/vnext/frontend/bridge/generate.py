#!/usr/bin/env python3
"""Generate this experiment's scalar transport bindings from typed/Model.chibi.

This recognizes a closed declaration subset, not Chibi programs. It executes no
Chibi expressions or input code. Unknown syntax and same-version layout drift
are errors. There is deliberately no schema configuration or dependency.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
MODEL = HERE.parent / "typed" / "Model.chibi"
OUTPUT = HERE / "generated"
# A digest is an ABI tripwire, not another field/kind definition. An intentional
# layout/kind change requires a new Model version and a reviewed digest here.
VERSION_LAYOUTS = {
    1: "3c80038a06b4ae925e60658a0506a816e8177630b6744367949e1f95d6b6a4bb",
    2: "9812f63cd2c2d7786cc536d5aed57665c4ab8c75327aaa744e25533039f2a3b1",
    3: "886b15e405bde5a0cb4223fc8986022797bd38d67946efaa3d6fa43c2fcc3ec6",
}
IDENT = r"[A-Za-z_][A-Za-z_0-9]*"
NUMBER = r"(?:0|[1-9][0-9]*)"


@dataclass(frozen=True)
class Model:
    version: int
    constants: tuple[tuple[str, str, int], ...]
    records: tuple[tuple[str, tuple[tuple[str, str], ...]], ...]
    tables: tuple[tuple[str, str], ...]
    enums: tuple[tuple[str, tuple[str, ...]], ...]

    def layout(self) -> dict:
        return {"constants": self.constants,
                "records": self.records, "tables": self.tables, "enums": self.enums}

    def digest(self) -> str:
        encoded = json.dumps(self.layout(), separators=(",", ":"), ensure_ascii=True)
        return hashlib.sha256(encoded.encode("ascii")).hexdigest()

    def word_tables(self) -> list[tuple[str, str, tuple[str, ...]]]:
        records = dict(self.records)
        return [(name, type_name, tuple(field for field, _ in records[type_name])
                 if type_name in records else ())
                for name, type_name in self.tables if type_name not in ("i64", "u8")]


def parse_model(text: str) -> Model:
    for line in text.splitlines():
        if line.lstrip().startswith("// vnext-bridge-") and not re.fullmatch(
                rf"// vnext-bridge-(?:version: [1-9][0-9]*|enum: {IDENT})", line):
            raise ValueError("malformed bridge marker")
    versions = re.findall(r"^// vnext-bridge-version: ([1-9][0-9]*)$", text, re.M)
    if len(versions) != 1:
        raise ValueError("Model.chibi must declare exactly one vnext-bridge-version comment")
    markers = list(re.finditer(rf"^// vnext-bridge-enum: ({IDENT})$", text, re.M))
    if not markers or markers[-1][1] != "end":
        raise ValueError("enum groups require a final end marker")
    # Preserve positions so markers cannot be hidden inside a declaration.
    source = re.sub(r"//[^\n]*", lambda match: " " * len(match[0]), text)
    prefix = re.match(r'\s*package vnext_typed\s+import syntax "project:syntax_diagnostics"\s*', source)
    if not prefix:
        raise ValueError("unsupported Model package/import declarations")
    cursor = prefix.end()
    constants, records, tables = [], [], []
    enums, active_enum, next_marker = [], None, 0
    values, declared = {}, set()
    phase = "constants"
    result_seen = False
    while cursor < len(source):
        whitespace = re.match(r"\s*", source[cursor:])
        cursor += whitespace.end()
        if cursor == len(source):
            break
        while next_marker < len(markers) and markers[next_marker].start() < cursor:
            marker = markers[next_marker][1]
            next_marker += 1
            if active_enum is not None and not enums[-1][1]:
                raise ValueError(f"empty enum group: {active_enum}")
            if marker == "end":
                if active_enum is None or next_marker != len(markers):
                    raise ValueError("misplaced enum end marker")
                active_enum = None
            else:
                if phase != "constants" or marker in [name for name, _ in enums]:
                    raise ValueError(f"duplicate or misplaced enum group: {marker}")
                active_enum = marker
                enums.append((marker, []))
        constant = re.match(rf"pub ({IDENT}): (u32|int) : ({NUMBER}|{IDENT})(?: \+ ({NUMBER}))?;", source[cursor:])
        record = re.match(rf"pub ({IDENT}) :: struct\s*\{{([^{{}}]*)\}}", source[cursor:])
        result = re.match(r"pub Result :: union\s*\{\s*Produced\(module: Module\)\s*\|\s*Rejected\(diagnostic: syntax\.Diagnostic\)\s*\}", source[cursor:])
        if constant:
            name, scalar, base, increment = constant.groups()
            if phase not in ("constants", "diagnostics") or (phase == "diagnostics" and scalar != "int"):
                raise ValueError(f"unsupported constant order: {name}")
            if base.isdecimal():
                value = int(base)
            elif base in values and values[base][0] == scalar:
                value = values[base][1]
            else:
                raise ValueError(f"unknown, forward or mixed-type constant reference: {base}")
            value += int(increment or "0")
            if value > (0xffffffff if scalar == "u32" else 0x7fffffff):
                raise ValueError(f"constant exceeds {scalar}: {name}")
            constants.append((name, scalar, value))
            if active_enum is not None:
                if scalar != "u32":
                    raise ValueError(f"enum member must be u32: {name}")
                enums[-1][1].append(name)
            values[name] = (scalar, value)
            size = constant.end()
        elif record:
            name, body = record.groups()
            if active_enum is not None:
                raise ValueError("enum group must end before records")
            if phase not in ("constants", "records"):
                raise ValueError(f"unsupported record order: {name}")
            phase = "records"
            fields, names, position = [], set(), 0
            while position < len(body):
                white = re.match(r"\s*", body[position:])
                position += white.end()
                if position == len(body):
                    break
                field = re.match(rf"({IDENT})\s*:\s*({IDENT})(\[\])?\s*;", body[position:])
                if not field:
                    raise ValueError(f"unsupported field declaration in {name}")
                field_name, field_type, array = field.groups()
                if field_name in names:
                    raise ValueError(f"duplicate field {name}.{field_name}")
                names.add(field_name)
                if name == "Module":
                    if not array or field_type not in {"u32", "i64", "u8", *(r[0] for r in records)}:
                        raise ValueError(f"unsupported Module table {field_name}: {field_type}")
                elif array or field_type != "u32":
                    raise ValueError(f"record field must be scalar u32: {name}.{field_name}")
                fields.append((field_name, field_type))
                position += field.end()
            if not fields:
                raise ValueError(f"empty record: {name}")
            if name == "Module":
                tables = fields
                phase = "result"
            else:
                records.append((name, tuple(fields)))
            size = record.end()
        elif result:
            name = "Result"
            if phase != "result":
                raise ValueError("Result must immediately follow Module")
            phase, result_seen = "diagnostics", True
            size = result.end()
        else:
            line = source.count("\n", 0, cursor) + 1
            raise ValueError(f"unsupported declaration at Model.chibi:{line}")
        if name in declared:
            raise ValueError(f"duplicate declaration: {name}")
        declared.add(name)
        if next_marker < len(markers) and markers[next_marker].start() < cursor + size:
            raise ValueError("enum marker inside declaration")
        cursor += size
    if not result_seen:
        raise ValueError("Model and Result declarations are required in order")
    used_records = [kind for _, kind in tables if kind not in ("u32", "i64", "u8")]
    if len(set(used_records)) != len(used_records) or set(used_records) != {r[0] for r in records}:
        raise ValueError("each scalar record must back exactly one Module table")
    if [(name, kind) for name, kind in tables if kind in ("i64", "u8")] != [("literals", "i64"), ("name_bytes", "u8")]:
        raise ValueError("only literals:i64[] then name_bytes:u8[] are supported side buffers")
    if next_marker != len(markers) or active_enum is not None:
        raise ValueError("misplaced or unterminated enum markers")
    for name, members in enums:
        if name in declared:
            raise ValueError(f"enum name conflicts with declaration: {name}")
        numbers = [values[member][1] for member in members]
        if len(set(numbers)) != len(numbers) or 0 in numbers:
            raise ValueError(f"enum members require distinct nonzero values: {name}")
    model = Model(int(versions[0]), tuple(constants), tuple(records), tuple(tables),
                  tuple((name, tuple(members)) for name, members in enums))
    expected = VERSION_LAYOUTS.get(model.version)
    if expected is None:
        raise ValueError(f"unreviewed bridge version: {model.version}")
    if model.digest() != expected:
        raise ValueError(f"Model layout/kinds changed without a reviewed version bump: {model.digest()}")
    return model


def upper(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).upper()


def chibi_source(model: Model) -> str:
    tables = model.word_tables()
    header = len(tables) + 1
    lines = ["// Generated by ../generate.py from typed/Model.chibi. Do not edit.",
             f"// Layout SHA-256: {model.digest()}", "package vnext_bridge", "",
             'import typed "project:vnext_typed"', "",
             f"pub Version: u32 : {model.version};", f"pub HeaderWords: int : {header};",
             "// All native scalar array element counts use signed 32-bit Chibi int.",
             "MaxWords: i64 : 2147483647;", "",
             "pub Buffers :: struct { words: u32[]; literals: i64[]; name_bytes: u8[]; }",
             "",
             "// Pack only already-produced/verified Modules. No semantic rechecking.",
             "// One flattened word destination, initialized by resize then filled once.",
             "// Literal/name arrays share invocation-owned COW storage, never copied here.",
             "// Borrow all three only during a synchronous C call; retain no pointers.",
             "pub Pack :: (module: typed.Module) -> Buffers {",
             "    count: i64 = i64(HeaderWords);"]
    for name, _, fields in tables:
        width = len(fields) or 1
        lines += [f'    assert(i64(length(module.{name})) <= (MaxWords - count) / {width}, "verified {name} word admission");',
                  f"    count += i64(length(module.{name})) * {width};"]
    lines += ["    words: u32[] = [];", "    words.resize(int(count));", "    words[0] = Version;"]
    for index, (name, _, _) in enumerate(tables, 1):
        lines.append(f"    words[{index}] = u32(length(module.{name}));")
    lines += ["    at: int = HeaderWords;"]
    for name, _, fields in tables:
        lines.append(f"    for (row in module.{name}) {{")
        if fields:
            for index, field in enumerate(fields):
                offset = "at" if index == 0 else f"at + {index}"
                lines.append(f"        words[{offset}] = row.{field};")
        else:
            lines.append("        words[at] = row;")
        lines += [f"        at += {len(fields) or 1};", "    }"]
    lines += ['    assert(at == int(count), "generated transport word count");',
              "    return {words: words, literals: module.literals, name_bytes: module.name_bytes};", "}"]
    return "\n".join(lines) + "\n"


def rust_source(model: Model) -> str:
    tables = model.word_tables()
    header = len(tables) + 1
    lines = ["// Generated by ../generate.py from typed/Model.chibi. Do not edit.",
             f"// Layout SHA-256: {model.digest()}",
             "// Same-process scalar views, not a persistent or endian-neutral format.",
             f"pub const VERSION: u32 = {model.version};", f"pub const HEADER_WORDS: usize = {header};",
             "pub const MAX_ELEMENTS: usize = i32::MAX as usize;"]
    for name, kind, value in model.constants:
        lines.append(f"pub const {upper(name)}: {'u32' if kind == 'u32' else 'i32'} = {value};")
    for name, members in model.enums:
        lines += ["", "#[derive(Debug, Clone, Copy, PartialEq, Eq)]", f"pub enum {name} {{"]
        lines.extend(f"    {member}," for member in members)
        lines += ["}", f"impl {name} {{", "    pub fn decode(value: u32) -> Option<Self> {", "        match value {"]
        lines.extend(f"            {upper(member)} => Some(Self::{member})," for member in members)
        lines += ["            _ => None,", "        }", "    }", "}"]
    for name, fields in model.records:
        lines.append(f"pub const {upper(name)}_WORDS: usize = {len(fields)};")
        lines.append(f"const _: [(); {len(fields) * 4}] = [(); std::mem::size_of::<[u32; {upper(name)}_WORDS]>()];")
        for index, (field, _) in enumerate(fields):
            lines.append(f"pub const {upper(name)}_{upper(field)}: usize = {index};")
    lines += ["", "#[derive(Debug, Clone, Copy, PartialEq, Eq)]", "pub enum FramingError {",
              "    HeaderTooShort,", "    Version { actual: u32 },",
              "    CountTooLarge { buffer: &'static str },", "    SizeOverflow,",
              "    WordCount { expected: usize, actual: usize },", "    InvalidSpan,", "}", "",
              "// These borrows must not outlive the synchronous caller. No allocation,",
              "// pointer reinterpretation, semantic inference, or type duplication.",
              "#[derive(Debug)]", "pub struct Arena<'a> {"]
    for name, kind, fields in tables:
        element = f"[u32; {upper(kind)}_WORDS]" if fields else "u32"
        lines.append(f"    pub {name}: &'a [{element}],")
    lines += ["    pub literals: &'a [i64],", "    pub name_bytes: &'a [u8],", "}", "",
              "impl<'a> Arena<'a> {",
              "    // The C adapter validates raw-pointer alignment, null/count pairs and",
              "    // byte extent before constructing slices. This safe decoder checks",
              "    // framing, not semantic validity: the producer already ran Verify.",
              "    pub fn from_parts(words: &'a [u32], literals: &'a [i64], name_bytes: &'a [u8])",
              "        -> Result<Self, FramingError>", "    {",
              "        if words.len() < HEADER_WORDS { return Err(FramingError::HeaderTooShort); }",
              "        if words[0] != VERSION { return Err(FramingError::Version { actual: words[0] }); }",
              "        for (buffer, count, width) in [(\"words\", words.len(), 4usize),",
              "                                       (\"literals\", literals.len(), 8),",
              "                                       (\"name_bytes\", name_bytes.len(), 1)] {",
              "            if count > MAX_ELEMENTS { return Err(FramingError::CountTooLarge { buffer }); }",
              "            if count.checked_mul(width).filter(|bytes| *bytes <= isize::MAX as usize).is_none() {",
              "                return Err(FramingError::SizeOverflow);", "            }", "        }",
              "        let mut end = HEADER_WORDS;"]
    for index, (name, kind, fields) in enumerate(tables, 1):
        width = f"{upper(kind)}_WORDS" if fields else "1"
        lines += [f"        let {name}_count = words[{index}] as usize;",
                  f"        if {name}_count > MAX_ELEMENTS {{",
                  f'            return Err(FramingError::CountTooLarge {{ buffer: "{name}" }});', "        }",
                  f"        let {name}_start = end;",
                  f"        end = end.checked_add({name}_count.checked_mul({width}).ok_or(FramingError::SizeOverflow)?)",
                  "            .ok_or(FramingError::SizeOverflow)?;",
                  f"        let {name}_end = end;"]
    lines += ["        if end != words.len() {",
              "            return Err(FramingError::WordCount { expected: end, actual: words.len() });", "        }",
              "        Ok(Self {"]
    for name, kind, fields in tables:
        suffix = f".as_chunks::<{upper(kind)}_WORDS>().0" if fields else ""
        lines.append(f"            {name}: words[{name}_start..{name}_end]{suffix}," if fields
                     else f"            {name}: &words[{name}_start..{name}_end],")
    lines += ["            literals,", "            name_bytes,", "        })", "    }", "}", "",
              "// A zero-length span is canonical only with start=0, even in a nonempty",
              "// table. This is a bounded indexing helper, not another IR verifier.",
              "pub fn span(start: u32, count: u32, total: usize)",
              "    -> Result<std::ops::Range<usize>, FramingError>", "{",
              "    let start = start as usize;", "    let count = count as usize;",
              "    if count == 0 {",
              "        return if start == 0 { Ok(0..0) } else { Err(FramingError::InvalidSpan) };", "    }",
              "    if start > total || count > total - start { return Err(FramingError::InvalidSpan); }",
              "    Ok(start..start + count)", "}"]
    return "\n".join(lines) + "\n"


def generated(model: Model) -> dict[str, str]:
    return {"Pack.chibi": chibi_source(model), "arena.rs": rust_source(model)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="reject stale generated bindings without writing")
    args = parser.parse_args()
    try:
        artifacts = generated(parse_model(MODEL.read_text(encoding="utf-8")))
        if args.check:
            for name, content in artifacts.items():
                path = OUTPUT / name
                if not path.is_file() or path.read_bytes() != content.encode("utf-8"):
                    raise ValueError(f"generated binding is stale: {path.relative_to(HERE)}")
        else:
            OUTPUT.mkdir(exist_ok=True)
            for name, content in artifacts.items():
                (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"bridge generation: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
