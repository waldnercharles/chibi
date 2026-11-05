# POSIX stamp width regression

Run `sh test/native_aot/posix_stamp/run.sh NATIVE_COMPILER LLVM_BIN` with an
accepted compiler and toolchain. The runner compiles the exact private POSIX
source plus byte-record cases, then a separate public `posix.Stamp` consumer,
at O0 and O2. It does not edit core sources.

The pure cases cover 2^31, 2^32, 2^63, u64 max, an asymmetric byte pattern,
two/four/eight-byte reads, signed wrap and i64 min, octal mode, nine-digit
nanoseconds, complete stamp field wiring, repeated output and unchanged input.
Run on Linux x86-64 and Darwin arm64 to exercise both stat layouts, including
Darwin's signed 32-bit device cast.

The real-path Python stat oracle uses integer nanoseconds (never float
timestamps), the C host's field order/signs and octal mode. Its owned fixtures
are a three-byte file, an empty file, a directory, a followed symlink, and
missing/broken/ENOTDIR paths. It checks repeated output, unchanged stamps and
file contents. No large or sparse file is needed: synthetic records cover
unrepresentable filesystem boundary values.

For an additional C-host differential, call the actual `build_io_stamp` from
`src/host/build_io.c:318` against those same stable paths in an accepted C host
build, and compare its complete strings to the Python and native results.
Do not substitute a low-32-bit comparator. Preserve compiler/toolchain
identities, exact source identity, commands, exits and both O0/O2 outputs.

Until that compiler/toolchain is accepted and the commands run, these are
unexecuted regression cases, not a native pass. Any earlier Graph/Driver,
source-closure, fixed-point or parity evidence predates this core change and
must be rebound to the changed source before it can establish readiness.
