# Authored prepared writer

`tools/compiler/prepared_writer` is the Chibi source of the prepared-module
encoder (CHIBIPRE format 15) and the executable closure container (CHIBICLO
version 1). Stage one will write its artifacts with it. Today it is checked
against the native writers; nothing calls it in production, and the bounded
compiler still takes its payload from the native finalizer.

## Model

`Write(module: Module) -> byte[]` encodes one module of authored functions:
requirements, imports, functions and entries. Format 15 also has events, states,
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

A call copies its record-array and union arguments, and checks every item of a
borrowed scalar array. So `Write` keeps every table in one function and passes
helpers only scalars, text and single records. It encodes all values once, from
the last back, into one byte pool, so a value's nested values are already
encoded when it is reached. The time is linear in the module size: about 36 KB/s
on bytecode and 110 KB/s on SLJIT in a Debug ASan build. Stage-one code follows
the same rule until borrowing covers record arrays.

`entry`, `owner`, `component` and `depth_limit` are keywords, so the model uses
`entry_name`, `owner_type`, `component_index` and `depth_bound`.

## Test

`tool language-prepared-writer-chibi` prepares the writer with
`core:strings`, `syntax_diagnostics`, `package_paths` and a sample package that
uses records, unions, arrays, imports, wide integers and floats. Its two cases
pass with 707 assertions in 30.8 s under the 60-second supervisor (macOS arm64,
2026-10-18):
- Every module's stage-zero plan is marshalled into the model and written on
  both compiler backends, then compared byte for byte with
  `execution_prepared_write`. The writer's own 1.3 MiB module takes 38 s on
  bytecode, so it is written on the compiled backend only.
- `Fingerprint` matches `execution_prepared_measure`, and `Closure` over the four
  smaller modules matches `compiler_artifact_write`, on both backends.
