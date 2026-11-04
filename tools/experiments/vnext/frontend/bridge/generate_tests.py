#!/usr/bin/env python3
"""Cold schema/transport reference tests; do not compile or interpret Chibi/Rust."""
import importlib.util
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bridge_generate", HERE / "generate.py")
generator = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = generator
spec.loader.exec_module(generator)
SOURCE = generator.MODEL.read_text(encoding="utf-8")
MODEL = generator.parse_model(SOURCE)

# Independent layout oracle, not a second schema input. Counts come from the
# real EchoJoin producer's CheckPair, including all side-table counts.
ECHO_JOIN = [
    ("functions", 2, 6, 12), ("signatures", 2, 3, 24),
    ("parameter_types", 4, 1, 30), ("blocks", 4, 4, 34),
    ("block_parameters", 5, 1, 50), ("values", 7, 2, 55),
    ("instructions", 6, 6, 69), ("edges", 3, 3, 105),
    ("arguments", 2, 1, 114), ("origins", 7, 3, 116),
    ("names", 2, 2, 137),
]


def reference_pack(rows, literals=(), name_bytes=b""):
    """Simple test oracle: flatten scalars, not source parsing or IR evaluation."""
    tables = MODEL.word_tables()
    counts = [len(rows[name]) for name, _, _ in tables]
    words = [MODEL.version, *counts]
    for name, _, fields in tables:
        for row in rows[name]:
            words.extend(row if fields else [row])
    return words, tuple(literals), bytes(name_bytes)


def reference_view(words, literals=(), name_bytes=b"", max_size=sys.maxsize):
    """Checked reference for decoder framing only. No source/body semantics."""
    tables = MODEL.word_tables()
    header = len(tables) + 1
    if len(words) < header:
        raise ValueError("header")
    if words[0] != MODEL.version:
        raise ValueError("version")
    for count, width in [(len(words), 4), (len(literals), 8), (len(name_bytes), 1)]:
        if count > 0x7fffffff or count * width > max_size:
            raise ValueError("extent")
    cursor, spans = header, []
    for count, (name, _, fields) in zip(words[1:header], tables):
        if not 0 <= count <= 0x7fffffff:
            raise ValueError("count")
        size = count * (len(fields) or 1)
        if size > max_size or cursor > max_size - size:
            raise ValueError("overflow")
        spans.append((name, cursor, cursor + size, len(fields)))
        cursor += size
    if cursor != len(words):
        raise ValueError("word count")
    result = {}
    for name, start, end, width in spans:
        result[name] = ([tuple(words[i:i + width]) for i in range(start, end, width)]
                        if width else words[start:end])
    return result, tuple(literals), bytes(name_bytes)


class GenerateTests(unittest.TestCase):
    def test_committed_outputs_match_schema(self):
        for name, expected in generator.generated(MODEL).items():
            self.assertEqual((generator.OUTPUT / name).read_bytes(), expected.encode("utf-8"))
        completed = subprocess.run([sys.executable, "-I", "-B", str(HERE / "generate.py"), "--check"],
                                   capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_generation_is_deterministic_and_comments_do_not_change_layout(self):
        changed = SOURCE.replace("// Target: 8 scalar bytes.", "// A changed explanatory comment.")
        changed = changed.replace("type: u32; definition:", "type : u32;\n    definition:")
        self.assertEqual(generator.generated(MODEL), generator.generated(generator.parse_model(changed)))
        for _ in range(8):
            self.assertEqual(generator.generated(MODEL), generator.generated(generator.parse_model(SOURCE)))

    def test_check_rejects_stale_output_without_rewriting(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bridge, typed = root / "bridge", root / "typed"
            (bridge / "generated").mkdir(parents=True)
            typed.mkdir()
            (bridge / "generate.py").write_bytes((HERE / "generate.py").read_bytes())
            (typed / "Model.chibi").write_text(SOURCE, encoding="utf-8")
            for name, contents in generator.generated(MODEL).items():
                (bridge / "generated" / name).write_text(contents, encoding="utf-8")
            target = bridge / "generated" / "arena.rs"
            target.write_text("stale\n", encoding="utf-8")
            checked = subprocess.run([sys.executable, "-I", "-B", str(bridge / "generate.py"), "--check"],
                                     capture_output=True, text=True, check=False)
            self.assertEqual(checked.returncode, 1)
            self.assertIn("generated binding is stale", checked.stderr)
            self.assertEqual(target.read_text(encoding="utf-8"), "stale\n")

    def test_enum_groups_are_complete_and_reject_malformed_markers(self):
        self.assertEqual(MODEL.enums, (("ScalarType", ("Bool", "I64")),
            ("Opcode", ("ConstantI64", "AddI64", "Jump", "Branch", "Return"))))
        changes = [
            ("// vnext-bridge-enum: end", ""),
            ("// vnext-bridge-enum: Opcode", "// vnext-bridge-enum: ScalarType"),
            ("// vnext-bridge-enum: Opcode", "// vnext-bridge-enum: end"),
            ("// vnext-bridge-enum: Opcode", "// vnext-bridge-enum: Empty\n// vnext-bridge-enum: Opcode"),
            ("// vnext-bridge-enum: ScalarType", "// vnext-bridge-enum: Block"),
            ("// vnext-bridge-enum: end", "// vnext-bridge-enum: end\n// vnext-bridge-enum: end"),
            ("// vnext-bridge-version: 1", "// vnext-bridge-version: 1\n// vnext-bridge-version: nope"),
            ("// vnext-bridge-enum: Opcode", " // vnext-bridge-enum: Opcode"),
            ("name: u32;", "name: u32;\n// vnext-bridge-enum: Misplaced\n"),
            ("pub I64: u32 : Bool + 1;", "pub I64: u32 : Bool + 0;"),
            ("pub Bool: u32 : 1;", "pub Bool: u32 : 0;"),
        ]
        for old, new in changes:
            with self.subTest(new=new), self.assertRaises(ValueError):
                generator.parse_model(SOURCE.replace(old, new, 1))

    def test_malformed_schema_fails_closed(self):
        replacements = [
            ("// vnext-bridge-version: 1", ""),
            ("// vnext-bridge-version: 1", "// vnext-bridge-version: 1\n// vnext-bridge-version: 1"),
            ("// vnext-bridge-version: 1", "// vnext-bridge-version: 2"),
            ("package vnext_typed", "package another"),
            ('import syntax "project:syntax_diagnostics"', 'import syntax "project:other"'),
            ("name: u32;", "name: u64;"),
            ("name: u32;", "name: u32[];"),
            ("name: u32;", "name: ^u32;"),
            ("name: u32;", "name: u32; name: u32;"),
            ("name: u32;", "name: u32 = 0;"),
            ("type: u32; definition: u32;", ""),
            ("functions: Function[];", "functions: Unknown[];"),
            ("functions: Function[];", "functions: Function;"),
            ("functions: Function[];", "functions: Function[]; duplicate: Function[];"),
            ("functions: Function[];", ""),
            ("literals: i64[];", "literals: u32[];"),
            ("name_bytes: u8[];", "name_bytes: i64[];"),
            ("pub I64: u32 : Bool + 1;", "pub I64: u32 : Missing + 1;"),
            ("pub I64: u32 : Bool + 1;", "pub I64: u32 : MaxNodes + 1;"),
            ("pub I64: u32 : Bool + 1;", "pub I64: u32 : 4294967296;"),
            ("pub I64: u32 : Bool + 1;", "pub I64: u32 : Bool * 2;"),
            ("pub MaxFunctions: int : 32;", "pub MaxFunctions: int : 2147483648;"),
            ("pub Signature :: struct", "pub Function :: struct"),
            ("pub Result :: union", "pub OtherResult :: union"),
            ("Produced(module: Module)", "Produced(module: Module, extra: u32)"),
        ]
        for old, new in replacements:
            with self.subTest(new=new):
                self.assertIn(old, SOURCE)
                with self.assertRaises(ValueError):
                    generator.parse_model(SOURCE.replace(old, new, 1))
        for extra in ["\nMain :: () -> int { return 0; }", "\npub Extra :: struct { value: u32; }", "/* comment */"]:
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                generator.parse_model(SOURCE + extra)

    def test_same_version_field_kind_and_table_drift_are_rejected(self):
        changes = [
            ("name: u32;\n    signature: u32;", "signature: u32;\n    name: u32;"),
            ("name: u32;", "renamed: u32;"),
            ("pub Bool: u32 : 1;", "pub Bool: u32 : 3;"),
            ("pub MaxFunctions: int : 32;", "pub MaxFunctions: int : 64;"),
            ("arguments: u32[];", "arguments: u32[]; extra: u32[];"),
            ("functions: Function[];\n    signatures: Signature[];", "signatures: Signature[];\n    functions: Function[];"),
        ]
        for old, new in changes:
            with self.subTest(new=new), self.assertRaisesRegex(ValueError, "layout/kinds changed"):
                generator.parse_model(SOURCE.replace(old, new, 1))

    def test_fixture_layout_budget(self):
        cursor = len(MODEL.word_tables()) + 1
        for (name, _, fields), (expected_name, count, width, start) in zip(MODEL.word_tables(), ECHO_JOIN):
            self.assertEqual((name, len(fields) or 1, cursor), (expected_name, width, start))
            cursor += count * width
        self.assertEqual(cursor, 141)
        self.assertEqual((cursor - 12) * 4 + 8 + 8, 532)
        self.assertEqual(cursor * 4 + 8 + 8, 580)
        self.assertEqual(dict(MODEL.records)["Value"], (("type", "u32"), ("definition", "u32")))
        self.assertNotIn("type", dict(dict(MODEL.records)["Instruction"]))

    def test_deterministic_scalar_roundtrip(self):
        randomizer = random.Random(20261104)
        for multiplier in (0, 1, 4, 32):
            rows = {}
            for (name, _, fields), (_, count, _, _) in zip(MODEL.word_tables(), ECHO_JOIN):
                rows[name] = ([tuple(randomizer.randrange(2 ** 32) for _ in fields) for _ in range(count * multiplier)]
                              if fields else [randomizer.randrange(2 ** 32) for _ in range(count * multiplier)])
            literals = (-(2 ** 63), -1, 0, 1, 2 ** 63 - 1) if multiplier else ()
            names = b"EchoJoin\x00\xff" if multiplier else b""
            packed = reference_pack(rows, literals, names)
            self.assertEqual(reference_view(*packed), (rows, literals, names))
            self.assertEqual(reference_pack(rows, literals, names), packed)
            # Full-width scalar bits survive a fixed-byte-order test transport.
            # This test encoding is not an on-disk IR format or production step.
            serialized = struct.pack(f"<{len(packed[0])}I", *packed[0])
            self.assertEqual(list(struct.unpack(f"<{len(packed[0])}I", serialized)), packed[0])
        self.assertEqual(reference_pack({name: [] for name, _, _ in MODEL.word_tables()})[0], [1] + [0] * 11)

    def test_boundary_framing_errors(self):
        empty = [MODEL.version] + [0] * 11
        bad = [([], "header"), ([0] * 12, "version"), (empty + [0], "word count"),
               ([MODEL.version, 1] + [0] * 10, "word count"),
               ([MODEL.version, 2 ** 31] + [0] * 10, "count")]
        for words, diagnostic in bad:
            with self.subTest(words=words), self.assertRaisesRegex(ValueError, diagnostic):
                reference_view(words)
        with self.assertRaisesRegex(ValueError, "overflow"):
            reference_view([MODEL.version, 0x7fffffff] + [0] * 10, max_size=0x7fffffff)
        with self.assertRaisesRegex(ValueError, "extent"):
            reference_view(empty, max_size=47)


if __name__ == "__main__":
    unittest.main()
