# Native backend execution

Run `sh test/native_aot/backend/run.sh NATIVE_COMPILER LLVM_BIN` on Linux x64
or macOS arm64. The `language-scalar-native` selector owns this fixture.
It needs LLVM22 and the authored native compiler, with no compiler-host or C
test runner.

The six former `package_compiler_chibi.c` cases have these owners:

| Former case | Current coverage |
| --- | --- |
| `native_bundle_starts_empty` | This runner writes complete arithmetic manifests and builds the Windows runtime from `native_link.Start`. Only the module producer owns `modules.txt`; native-build and native-link keep their separate program manifests. |
| `package_compiler_native_arithmetic_is_total` | The same 12 low-level operations and eight boundary values produce 668 calls per module. A wider integer oracle supplies exact i32 answers. O0/O2 modules are verified for Linux, macOS and Windows, and executed by the same authored bundle parser, linker and call ABI used by cross-target CI. |
| `package_compiler_native_arrays_run_and_check_indexes` | Every expected-accepted corpus source with the original array, union-array, record and switch prefixes, plus both readonly/writable alias orders, compiles and runs at O0/O2. The original bodies and full i32 answers stay intact. The managed-value poison/count oracle requires zero live allocations before exit, including `array_drops`. Read/write bounds failures preserve status134 and exact index/length text; INT_MIN/MAX cases exercise diagnostic formatting. |
| `package_compiler_native_runs_are_bounded` | Authored supervision enforces the original 500ms tool deadline, kills and reaps its descendant group, and bounds busy O0/O2 native programs. The original dense switch executes as O2 PIC in a shared image. Parent-only SIGINT/SIGTERM cancellation and its distinct130/143 statuses are covered by `test/native_cli/foreground.py`; explicit process-group cancellation is in `Processes.chibi`. |
| `package_compiler_native_edit_and_continue_keeps_state` | `test/native_aot/generations/run.sh` executes legacy library generations, including the original Frame-only shape without Main, preserves retained fields by name/type, keeps new initializers, and does not rerun Main after replacement. It also covers numeric migration, independent generations and rejected edits. |
| `stack_overflow_leaves_other_faults` | The C report handler had no product caller and is retired with its only harness. Native programs retain the OS fault policy; the authored supervisor checks a real SIGSEGV exits by that signal without an invented stack-overflow report. Native stack probes remain in the compiler and large-frame cross-target fixtures. |

When `CHIBI_NATIVE_BUNDLE` is set, this runner publishes the established
`0-total-0.o`, `0-total-2.o`, `modules.txt` and Windows `runtime.o` into the
macOS/Windows artifact directories. Every platform uses the original
`probe_NAME(ptr to i64 arguments) -> i64` adapter ABI. The normal target
consumer still requires and executes both independent program manifests;
its `--modules` mode runs the same modules locally during preparation.

The source producer's row, rejection and library-manifest contracts remain in
`test/native_aot/scalar_producer`. The C census still uses its separate
compiler-host consumer until that census is migrated; it is not needed here.
