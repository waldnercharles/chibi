# POSIX stamp width regression

Run `sh test/native_aot/posix_stamp/run.sh NATIVE_COMPILER LLVM_BIN` with an
accepted compiler and toolchain, Python 3, and an installed GNU C23 compiler
(`CC`, default `cc`, as a single executable path). The runner compiles the
exact private POSIX source plus byte-record cases, then a separate public
`posix.Stamp` consumer, at O0 and O2. It does not edit core sources.

The pure cases cover 2^31, 2^32, 2^63, u64 max, an asymmetric byte pattern,
two/four/eight-byte reads, signed wrap and i64 min, octal mode, nine-digit
nanoseconds, complete stamp field wiring, repeated output and unchanged input.
Run on Linux x86-64 and Darwin arm64 to exercise both stat layouts, including
Darwin's signed 32-bit device cast.

The runner also builds `host.c` with the actual `src/host/build_io.c` and
`src/support/xalloc.c`, not a copied stamp implementation or stubs. It uses
GNU C23 mode (`-std=gnu2x`) with strict warnings at O2 and platform dead
stripping to exclude unrelated host operations and their dependencies. A compiler or link failure fails
the test; the C comparison is required.

The real-path Python stat oracle uses integer nanoseconds (never float
timestamps), the C host's field order/signs and octal mode. The owned fixtures
are a three-byte file, an empty file, a directory, a followed symlink, and
missing/broken/ENOTDIR paths. Both the C host and native probe use those paths.
It compares their complete output strings to Python twice per optimization
level, including all three empty results, and checks unchanged stamps and
file contents. No large or sparse file is needed: synthetic records cover
unrepresentable filesystem boundary values.

Preserve native/C compiler and toolchain identities, exact source identity,
commands, exits and both O0/O2 outputs. Do not substitute a low-32-bit comparator.

Until that compiler/toolchain is accepted and the commands run, these are
unexecuted regression cases, not a native pass. Any earlier Graph/Driver,
source-closure, fixed-point or parity evidence predates this core change and
must be rebound to the changed source before it can establish readiness.
