# Native task prerequisites

Run `sh test/native_aot/tasks/run.sh NATIVE_COMPILER LLVM_BIN` with a native
compiler built from this branch and LLVM 22 or later. The suite uses LLVM and
LLD directly on macOS arm64 or Linux x64. Its LLVM allocator audit checks that
every program releases its coroutine frames and scheduler storage at shutdown.

The programs cover first-suspension order, `yield`, direct `await Function()` results,
statement-only `await handle`, cancellation of inline awaited frames,
cancellation of a parent and its descendants,
children surviving a completed parent, 1,024 concurrent tasks, 8,192 sequential
task lifetimes, stale handles after row reuse, and a nonwaiting program entry.
Every program runs at O0 and O2.

Task handles contain a generation and row index. Finished rows return to a free
list. The ready list preserves first-suspension order independently of row reuse; each
update resumes its original ready tasks at most once and then compacts the
list. Parent and sibling links make subtree cancellation linear in the number
of cancelled tasks without recursive scheduler calls. Rows and the ready list
grow geometrically to their peak live workload and are released with the
program. A completed parent detaches its children so they continue running.
This order matches the frozen VM: `archive/src/execution/program_tasks.c` runs
a task to its first suspension before appending its activation, and
`archive/src/execution/program.c` walks those activations in append order.

Native generations export `chibi_host_tasks_pending() -> i64`,
`chibi_host_tasks_poll() -> i32`, and
`chibi_host_tasks_cancel_all() -> i32`. The last two return zero. A host keeps
a retired generation loaded while it has pending tasks, polls it at safe
points, and calls `cancel_all` before unloading it. This ABI does not prove the
host integration; the live Edit and Continue tests own that check.

This is a tested subset, not the VM deletion gate. Awaiting a handle's
`T | Cancelled` result, deadlines and delays, managed coroutine values,
component dependencies, and full task failure propagation still need native
implementations. The rejection cases keep the first four unsupported forms
from compiling as successful programs. LLVM versions before 22 are unsupported.
