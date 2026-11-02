# Edit and Continue

Status: implemented for standalone programs, 2026-10-29. Applies to standalone programs run with `chibi --watch`
and to the shared program session (`Chibi::program`, `src/authoring/program.c`) that
Studio can also use. Studio Live used to publish through the source registry
(`src/runtime/behaviors/source_publication.c`, archived 2026-10-30); it moves to the
program session path when Studio is ported. Both end in the same
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

The preceding sections describe VM-backed sessions. The native path uses a Chibi host
(`apps/host/Host.chibi`) and shared libraries loaded through `foreign import`. It currently
runs on Linux x86-64 and macOS arm64. `Main` runs once, and a saved edit changes the next `Frame` without
restarting the process.

Each successful build is a generation, loaded from a unique `.so` path. A generation owns
its retained globals and libc-backed runtime context. The host validates an incoming
generation, migrates state into it, then switches the active `Frame` entry between calls.
A running `Frame` finishes before the switch. Suspended tasks keep running in their
original generation; the host keeps that library loaded and polls its tasks between
active Frames. Once its tasks finish, the host releases its scheduler and retained state
and unloads it. Program exit cancels tasks in every remaining generation.

Retained numbers, bools and standalone strings use `Function.variable` keys. Numeric/bool
records use one key per leaf, such as `Frame.position.x`, so migration follows field names
rather than layout. Scalar values transfer as words; strings copy their bytes into the
incoming generation before the original library can unload. The host reads and writes
state through the generation's `chibi_host_retained_*` symbols.

| Change | Result |
| --- | --- |
| Same leaf key and type | Value kept. |
| New field or retained binding | Starts from its initializer. |
| Removed field or binding | Not copied; its original storage is released when that generation unloads. |
| Same leaf key, another number or bool type | Converted as the language converts: integers clamp, float-to-integer conversions truncate and saturate with NaN becoming 0, and numbers convert to bool by inequality to zero. |
| Same leaf key changes between a string and a scalar | Incoming generation rejected; the running generation continues. |

The host checks incompatible leaf types before writing incoming state. Compile, link,
load or required-entry failures leave the accepted generation running, so a later valid
edit can apply. A loaded candidate that fails validation is set aside as `rejected.so`.
Native assertion failures in `Main`, `Frame` or a polled task terminate the host with
status 134. The VM's pause-and-resume behavior above does not apply to the native host.

### Status

Native programs do this on Linux x86-64 and macOS arm64: `chibi run --native --watch [-O0|-O2] [--llvm DIR] PATH`
builds the library or package source in PATH's folder as a generation (`chibi build --native --shared`), runs
it under the loop host (`apps/host/Host.chibi`, written in Chibi and built natively), and rebuilds
and publishes each saved edit as the next generation. The host calls `Main` once, calls `Frame`
until it returns a nonzero status, which is the exit status, and swaps `next.so` in between two
Frames: it matches the retained states of the two generations by name hash and type
(`chibi_host_retained_*`) and sets the new generation's. The old generation unloads once
its tasks finish. A generation that fails to build is reported and the running one
continues; one that fails to load is set aside as
`rejected.so`. The `native-build` case `WatchesAndSwapsARunningLoop`
(`test/native_aot/native_build/BuildTests.chibi`) edits a running loop and checks the exit status that
only a surviving count gives; `SwapsARunningLoopKeepingState` does the same with the editor played by the
test. Ordinary packages build as generations when they define
zero-argument `Main` and `Frame` with an int result ABI (`() -> int`; a non-entry
void `Frame` also has the existing int-zero adaptation). `Main` and `Frame` cannot wait;
other functions may start supported tasks. The isolated real-driver test
`test/native_aot/generations/run.sh` swaps a package while inserting a function and a retained
binding, and changing retained numeric types. Retained keys remain `Function.variable` names;
package generation function IDs are local indices, not stable prepared-artifact identities.
Duplicate function names across imported packages are rejected rather than aliased.

Package generations have a private libc-backed runtime context, so arrays, string
operations and assertions keep their native behavior. An assertion failure exits the host
with status 134. Retained numbers, bools, strings and plain records containing those values migrate by
named leaves. Record fields can be reordered or added; a new leaf takes its initializer.
Initializers must be scalar/string literals or constant record construction. Arrays,
unions and task handles remain unsupported as retained state. Local record snapshots own their string
references, so replacing a retained field keeps earlier snapshots valid. Changing a shared leaf
between a string and a scalar rejects the incoming generation before state is written.
Strings are copied into the incoming generation, including strings whose bytes originally
lived in the old object's constant data.

The legacy `library` form keeps its scalar-literal retained-state ABI: its bulk word
manifest exposes initialized numbers and bools before any function runs. Record and
string retained state uses ordinary package generations and the per-key native host API;
lazy or managed states expose no legacy bulk manifest.

A retired generation stays loaded while its tasks are pending. The host polls those tasks
between active Frames, then cancels/releases their scheduler storage and retained values
before unloading the generation. On program exit it cancels every generation's tasks.
`test/native_aot/generations/run.sh NATIVE_COMPILER LLVM_BIN` exercises native package
reloads, numeric conversions, record field migration, retained strings, incompatible
schema rejection, runtime allocation and assertions. The VM and C hosts remain until the
other migration gates pass.

## Limits

- One thread; updates apply only between `Main`/`Frame` calls.
- A program that loops inside `Main` is not patched (see above); use `Frame`.
- Game projects reload through Studio, not `--watch`.
