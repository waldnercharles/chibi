# Authored prepared writer

`tools/compiler/prepared_writer` is the Chibi source of the prepared-module
encoder (CHIBIPRE format 18) and the executable closure container (CHIBICLO
version 1). Stage one will write its artifacts with it. It is checked against
the native writers, and the bounded compiler writes its modules and closures
with it (`docs/bounded-authored-compiler.md`).

## Model

`Write(module: Module) -> byte[]` encodes one module of authored functions:
requirements, imports, functions and entries. Format 18 also has events, states,
queries, bindings and actions; the writer emits them as empty. The caller's
lowering supplies the target ABI hash in the header, because it hashes C
layouts.

Authored records hold only text, `int` and `bool` fields, so `Module` is one
union case of flat pools:
- Every 64-bit value (ids, layouts, scalar bits, union tags, fingerprints) is an
  index into `wide: uint64[]`, whose entry 0 is zero for an absent value.
- A nested value lists its fields, payload or items as a range of `values` after
  itself.
- Each function names 21 consecutive `spans`, one range per table.

`Fingerprint(bytes)` is the FNV-1a import fingerprint. `Closure` writes the
CHIBICLO header and rows sorted bytewise by name.

## Cost

`Write` keeps every table in one function. A helper such as `InstructionBytes`
encodes one row from a single record, scalars and the pools the row indexes,
which calls share rather than copy (see `docs/code-language.md`). It passes the
row's fields to `Words` or `Ints` as one array literal, and those encode every
field as a varint. The writer's time goes to calls and copies rather than to
arithmetic. In a Debug ASan build (macOS arm64, 2026-10-19), each of these
costs, on SLJIT and on bytecode:

- a call that returns an array: about 12 µs and 56 µs;
- a scalar call, such as `Zig` or `Flag`: 1.8 µs and 5 µs;
- a record copied into a local, as `v := values[at]` does: 7 µs and 13 µs, so
  rows are passed to their helper in place;
- `array_add` onto a local array: 0.25 µs and 4 µs.

Until 2026-10-19 every field was its own call that built and returned an array,
and every string was read one `ByteAt` call per byte. Now a string's bytes come
from one `strings.Bytes` call, and a source row reuses the encoded name of the
previous source's asset, since every instruction's source repeats its file's
path. All values are encoded once, from the last back, into one byte pool, so a
value's nested values are already encoded when it is reached. The time is linear
in the module size: about 100 KB/s on bytecode and 500 KB/s on SLJIT, against
27 KB/s and 120 KB/s before. `Fingerprint` walks every byte at about 150 KB/s
on bytecode.

`entry`, `owner`, `component` and `depth_limit` are keywords, so the model uses
`entry_name`, `owner_type`, `component_index` and `depth_bound`.

## Test

`tool language-prepared-writer-chibi` prepares the writer with
`core:strings`, `syntax_diagnostics`, `package_paths` and a sample package that
uses records, unions, arrays, imports, wide integers and floats. Its two cases
pass with 1,045 assertions in 22.0 s under the 60-second supervisor (macOS
arm64, 2026-10-19):
- Every module's stage-zero plan is marshalled into the model and written on
  both compiler backends, then compared byte for byte with
  `execution_prepared_write`. Modules above 512 KiB, the writer's own 1.3 MiB
  and `core:strings`' 0.8 MiB, take 14 s and 10 s on bytecode, so they are
  written on the compiled backend only.
- `Fingerprint` matches `execution_prepared_measure`, and `Closure` over the four
  smaller modules matches `compiler_artifact_write`, on both backends.
