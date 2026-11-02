Run `sh test/native_aot/managed_values/run.sh NATIVE_COMPILER LLVM_BIN` on macOS
arm64 or Linux x64. It compiles real source at O0 and O2, poisons freed bytes,
and checks that every context allocation is released.

The program covers string-bearing record snapshots, nested records, dynamic NUL
bytes, copy and replacement, self-assignment, borrowed parameters, written
references, returned temporaries, scope exits, arrays of records and union
payloads. Record arrays exercise element replacement, append, copy, slice,
resize, removal and temporary-element reads. The baseline compiler leaks on this
program and returns 99 through the allocator audit.

The native generation and CLI watch suites separately exercise retained records
with strings across live edits, field reordering, new-field initializers and
incompatible schema rejection. Arrays and unions remain unsupported as retained
bindings. Records that hold array backing still need recursive ownership before
they can participate in this counted aggregate path.

The emitter probe separately exercises native `ValueCopy`/`ValueDrop` directly:
an owned text record survives dropping its original, its array copy survives
dropping the source array, and all owners are released. Invalid scalar operands
are rejected. Native array ranges and map payloads share the generated record
helpers; frontend record expansion remains specialized until array backing
joins the same lifecycle.
