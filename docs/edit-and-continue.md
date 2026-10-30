# Edit and Continue

Status: implemented for standalone programs, 2026-10-29. Applies to standalone programs run with `chibi --watch`
and to the shared program session (`Chibi::program`, `src/authoring/program.c`) that
Studio can also use. Game projects keep Studio Live, which publishes through
`src/runtime/behaviors/source_publication.c`; both end in the same
`execution_program_prepare_replace` / `execution_program_commit_replace` pair.

## What the user sees

```chibi
package demo

Frame :: () {
    ticks: retain int = 0
    ticks += 1
    print_string(format_text("tick {}", ticks))
}
```

`chibi --watch .` keeps running. Edit and save: the next frame runs the new body and
`ticks` keeps counting. `Main` is not called again. A syntax or type error prints its
diagnostic once, the old code keeps running, and the next good save applies.

## Program shape

- `Main :: ()` is startup. It runs once, if present.
- `Frame :: ()` is the per-frame entry, called repeatedly (about 60 Hz) until Ctrl-C.
  It returns nothing to continue, or a nonzero `int` exit status (1..255) to stop.
- A program with `Main` only runs and exits. Without `Frame` there is nothing to
  continue, so `--watch` keeps polling and applies edits but never re-runs `Main`.
- The session API (`program.open/update/call_entry/has`) takes the function by name,
  so Studio can drive the same loop. `open` takes an empty entry for such programs.

## Why a per-frame entry, not on-stack replacement

Replacing the body of a function that is mid-call needs a map from the old frame
(program counter, locals, the managed values they hold) to the new one. The VM and the
SLJIT code have no such map, and a wrong map corrupts a running program silently.
The runtime already has a safe boundary: between top-level calls the program is
quiescent, and replacement there is validated (the old module is checked, system work
is cancelled, references are rebound, old generations are freed). A per-frame entry
makes every iteration of the user's loop such a boundary, so edits apply without any
frame remapping. The cost is that a hand-written `for !application.stopped()` loop
inside `Main` cannot be patched; its body changes take effect when the loop is
restarted. On-stack replacement at loop back-edges remains possible later, because
the VM already checks the step budget there.

## What is patched when

| Change | Applied |
| --- | --- |
| Function body, new function, new package | At the next call after the update commits; all calls resolve through the accepted generation. A frame already executing finishes in the old code. |
| `retain` value, same type | Kept. |
| `retain` value, changed type or record layout | Converted by `program_migrate_value` (`docs/managed-pointers.md`, "Hot reload and cell storage"); unconvertible values restart from their initializer. |
| New field, parameter or `retain` binding | Applied live; new state starts from its initializer. |
| Removed state | Dropped. |

The runner updates between frames, polling sources about every 250 ms.
A "rude edit" (one that forces a restart) is anything the replacement already resets:
pointer cells whose pointee layout changed, and rows a system's query manages when its
shape changes. These reset only that state, never the process.

## Errors

`program.update` compiles the changed packages into a candidate and prepares the
replacement without touching the accepted generation. A failure leaves the old code
running and reports `file:line:col: message`; an identical failed snapshot is not
reported twice. A runtime failure in `Main` or `Frame` prints its diagnostic and
pauses the program: with `--watch` the next good save resumes it (calling `Main` again
if `Main` had not finished, otherwise `Frame`); without `--watch` the run exits with
status 1.

## Native code

The goal is the same behavior with no VM: a native program's loop picks up edited code at a
safe point with its state intact, and `Main` does not run again. This section is the design;
`Status` below says what has landed. It follows Odin's hot-reload template (code in a
reloaded shared library, state in host memory) and Live++/.NET EnC's rule that a bad edit
never damages the running program.

**Generations.** Each successful native build of the program is a *generation*: a shared
library (`.so`, `.dylib`, `.dll`) with a unique file name, so the loader never returns the
previous copy. A generation exports a manifest (`chibi_natives_<Name>`, as native libraries
do) listing `Main`, `Frame` and the state schema. The host owns the state; no generation
owns anything that outlives it.

**State is a record.** The state of a native program is one record of the program's
`retain` bindings, passed to `Frame` and returned by it. The compiler lowers `retain`
bindings to that record (a hidden parameter and result of `Frame`), so source still reads
as in the VM. The export ABI passes a record of scalars as 64-bit words in field order,
and the manifest carries the schema: each field's name, scalar type and word index.

**The safe point is between `Frame` calls.** The host loop is: load generation 1, build
the initial state, call `Main` once, then call `Frame(state)` repeatedly. When a new
generation is built, the host loads it, validates it, and swaps it in before the next
`Frame`; a frame that is running finishes in the generation it started in. The old library
is closed after the swap. Nothing executes inside a generation that is being replaced.

**Migration by schema.** The host compares the old and new schemas field by field, by name:

| Change | Result |
| --- | --- |
| Same name and scalar type | Value kept. |
| New field | Starts from its initializer. |
| Removed field | Dropped. |
| Same name, other scalar type | Converted when the VM's `program_migrate_value` converts it; otherwise reset to the initializer. |

A migration never partly applies: the new state is built whole, then swapped.

**Failure leaves the old generation running.** A build that fails to compile or link, a
library that fails to load, a manifest of another ABI, a missing `Frame`, or a schema the
host cannot map is reported once as `file:line:col: message` (or the loader's reason) and
the running generation continues. The next good build applies. A `Frame` that fails at run
time pauses the loop, as in the VM.

**Where it runs.** The host loop is Chibi code (`apps/chibi`), loading libraries through
`foreign import`, so nothing about it depends on the C VM. Until `foreign import` lands the
VM session above remains the implementation behind `chibi --watch`.

### Status

Native programs cannot do this yet. Open pieces, in order: scalar records across the export
ABI with a schema (`llvm_native/Emit.chibi`); `retain` and `Frame` in the authored compiler
as the state-record lowering; the generation-swap host in Chibi; `chibi run --native
--watch`; the test that edits source while the loop runs and checks that state survives
(Linux x86-64 first, as native libraries are).

## Limits

- One thread; updates apply only between `Main`/`Frame` calls.
- A program that loops inside `Main` is not patched (see above); use `Frame`.
- Game projects reload through Studio, not `--watch`.
