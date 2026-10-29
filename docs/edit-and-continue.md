# Edit and Continue

Status: design, 2026-10-29. Applies to standalone programs run with `chibi --watch`
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
  It returns nothing to continue, or an `int` exit status (0..255) to stop.
- A program with `Main` only behaves as before: it runs and exits. Without `Frame`
  there is nothing to continue, so `--watch` applies edits but never re-runs `Main`.

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

## Native code later

Every call already goes through a function id resolved against the accepted module.
Native code (workstream E) joins by registering its compiled body under the same id in
the generation, so a swap replaces one table entry and the old body stays alive until
its last frame returns. Nothing here depends on the VM or SLJIT specifically.

## Limits

- One thread; updates apply only between `Main`/`Frame` calls.
- A program that loops inside `Main` is not patched (see above); use `Frame`.
- Game projects reload through Studio, not `--watch`.
