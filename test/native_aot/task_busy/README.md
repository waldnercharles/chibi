# Native test task effects

Run `sh test/native_aot/task_busy/run.sh NATIVE_COMPILER LLVM_BIN` on macOS
arm64 or Linux x64. The probe uses the real native begin/end/expect runtime
functions and real child processes at O0 and O2.

An active test rejects yielding, pending inline awaits, every named task
command and spawning. Commands include handle/result awaits even when ready,
zero and positive delays, cancellation and running-state tests. Earlier effects remain visible; later effects do not run.
A spawn evaluates its arguments, then fails before starting its body. Busy is
an unclassified failure, so expecting AssertionFailed cannot accept it.
Immediate-ready inline authored awaits pass because they begin no command.
Ordinary quota scopes allow tasks; a quota-only source also checks that it gains
no Busy calls in its emitted IR.

Generated `chibi test` dispatch and VM/native differential outcomes belong to
the native CLI test suite. This probe isolates the runtime instruction guards.
