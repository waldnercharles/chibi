# Native package generations

On Linux x86-64, run the real native compiler and host regression with LLVM 22
or later, using absolute paths:

```sh
sh test/native_aot/generations/run.sh /path/to/chibi /path/to/llvm/bin
```

The runner compiles `apps/host/Host.chibi` and actual shared generations through
the native compiler. It compiles edits while the original generation runs and
publishes each edit by renaming `next.so`. No VM build or compiler host runs.

The package swaps check Main running once, numeric conversion, retained record
fields surviving reordering, new fields keeping their initializers, and
retained strings surviving the old mapping. Library and sibling-import cases
exercise both shared compilation routes. Runtime cases cover arrays, formatted
strings, assertions, recursive retained writes, and process arguments.

Invalid Main or Frame signatures and waiting entry functions must produce
specific diagnostics while preserving the prior shared object byte for byte.
An incompatible retained schema must leave the original generation running.

Tasks started by Main must advance before any edit arrives, and an active
task assertion must propagate status 134.

The task failure case keeps a worker suspended until the edited Frame signals
that it is running. The worker then resumes an old-only helper that records a
marker and fails an assertion. The host must return 134, proving that a retired
generation's task failure reaches the caller. The CLI watch regression owns
the corresponding successful old-generation continuation case.
