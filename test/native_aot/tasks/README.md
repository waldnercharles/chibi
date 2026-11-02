# Native task prerequisites

Run `sh test/native_aot/tasks/run.sh NATIVE_COMPILER LLVM_BIN` with a native
compiler built from this branch and LLVM 22 or later. The suite uses LLVM and
LLD directly on macOS arm64 or Linux x64. Its LLVM allocator audit checks that
every program releases its coroutine frames and scheduler storage at shutdown.
The sequential reuse and result fixtures also cap the peak at 32 allocations
and 64 KiB, so retaining completed frames or failing to reuse rows cannot pass.

The programs cover first-suspension order, `yield`, direct `await Function()` results,
statement-only `await handle`, numeric and void `T | Cancelled` results,
repeated joins, aliases and self-assignment, cancellation of inline awaited frames,
cancellation of a parent and its descendants,
children surviving a completed parent, 1,024 concurrent tasks, 8,192 sequential
task lifetimes, stale handles after row reuse, and a nonwaiting program entry.
Every program runs at O0 and O2.

Timed programs also cover `await Delay(seconds)`, cancellation deadlines measured
from task start, repeated requests that shorten but never extend a deadline,
parent-child expiration, inline awaited delays, and negative/NaN durations.
The clock fixture supplies simulation time, including a negative time domain;
the foreign-call fixture composes delays with ordinary platform clock calls.

Task handles contain a generation and row index. Values own observers while
the program independently owns active execution. Dropping every observer does
not cancel active work. A completed promise remains only while observed; its
last observer destroys the frame and returns its row to the generation free list.
A cancelled observed row keeps its cancelled state without a coroutine frame.
The ready list preserves first-suspension order independently of row reuse; each
update resumes its original ready tasks at most once and then compacts the
list. Parent and sibling links make subtree cancellation linear in the number
of cancelled tasks without recursive scheduler calls. Rows and the ready list
grow geometrically to their peak live workload and are released with the
program. A completed parent detaches its children so they continue running.
This order matches the frozen VM: `archive/src/execution/program_tasks.c` runs
a task to its first suspension before appending its activation, and
`archive/src/execution/program.c` walks those activations in append order.

Untimed rows remain 72 bytes. Timed modules add start, deadline and wake-time
doubles, making each row 96 bytes. An update samples time once and expires due
subtrees before resuming any continuation. Standalone entries sleep while all
tasks wait, waking for the earliest delay or cancellation deadline. Host polling
does not sleep. Modules without timed operations emit no clock code.

Native generations export `chibi_host_tasks_pending() -> i64`,
`chibi_host_tasks_poll() -> i32`, and
`chibi_host_tasks_cancel_all() -> i32`. The last two return zero. A host keeps
a retired generation loaded while it has pending tasks, polls it at safe
points, and calls `cancel_all` before unloading it. This ABI does not prove the
host integration; the live Edit and Continue tests own that check.

Timed generations additionally export `chibi_host_tasks_set_time(f64) -> i32`
and `chibi_host_tasks_poll_at(f64) -> i32`. A host sets its simulation time before
starting work, then supplies time at each poll. Both return 0 for a finite time;
NaN or infinity returns 1 and leaves time and tasks unchanged. Without explicit
host time, the scheduler uses the platform monotonic clock. These optional
exports preserve the existing no-argument polling ABI.

This is a tested subset, not the VM deletion gate. Managed coroutine values,
task-valued parameters, component dependencies, and full task failure propagation
still need native implementations. Task parameters remain closed until cancelling
an active ancestor can unwind every live call safely. The runtime
stores the canonical success type, with void distinct from its internal int
result. Running-handle conditions and assignment across different producers
remain separate source-producer gaps. The rejection
cases keep cross-type assignments and unsupported task parameters/managed frames
from compiling as successful programs. LLVM versions
before 22 are unsupported.
