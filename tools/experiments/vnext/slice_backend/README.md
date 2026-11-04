# Cranelift capability probe

This experiment uses two hand-authored typed bodies, Echo and Join, through the same lowering function into eager JIT and AOT object output. It has no Chibi frontend and must not be described as completed Chibi compilation, the vertical-slice milestone or a compilation-speed result. There is no parser, interpreter or fallback path. Only Linux x86-64 System V is admitted.

The six direct Cranelift dependencies are pinned to 0.137.1. Preserve Cargo.lock and its registry checksums. Use an isolated, checksum-verified official Rust 1.99.0 installation and workspace-owned CARGO_HOME/CARGO_TARGET_DIR; never alter system tools or shell profiles. Dependency setup/build logs and release/archive identities are retained outside the repository by the coordinating task.

After explicit CPU-window approval, build with `cargo build --release --locked --offline --jobs 2`, using a previously fetched dependency set. Invoke the resulting probe with a new object-output path. It verifies and compiles both bodies, finalizes JIT code eagerly, checks six calls, emits the same bodies through ObjectModule, exclusively creates the object and releases the JIT owner only after calls finish. Link `aot_caller.c` and that object with the existing target C compiler, then run the executable. Both execution results are required. C caller checks include the C bool parameter ABI and wrapping i64 addition.

The caller mirrors Echo/Join expectations from the adjacent slice corpus and adds an overflow check. The extra case is a backend check only. It does not claim the Chibi parser accepts new source. CLIF verification is mandatory. The tiny internal typed-body records are probe input, not a public serialization or a new language implementation.

No timing from this fixture is a 100,000 LOC/sec result. Real frontend acceptance, foreign calls from generated code, full corpus coverage, runtime quality and representative full-compilation measurement remain separate gates. Preserve failed setup/build/run receipts and do not silently choose another version or backend.
