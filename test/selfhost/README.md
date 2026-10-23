# Authored self-host regression closure

`Regressions.chibi` aggregates bounded pure regression entries for bootstrap
selection/package contracts, authored parser facts, and native target metadata.
It returns failure descriptions; an empty array passes.
The parser fixture alone uses the existing raw syntax oracle. This suite does
not include the LLVM emitter/source adapter or replace independent native tests.

`sources.json` is an explicit repository-file to virtual-source mapping. The
first file is the root package at `regressions/Tests.chibi`; its stable exported
`Tests` identity is `0xacf18e429e509cc3`. Collection roots and paths are virtual,
not filesystem reads performed by the compiled tests. List required source
closure files explicitly, including every file in each selected production
package. The runner rejects omitted production package members. Pure fixture
files may omit their separate assert-native wrappers. Source text is strict
UTF-8 and repository paths may not traverse symlinks.

The compiler-recovery workflow first proves two compiler generations equal. It
then uses that exact stage-two compiler and the checksum-verified runtime to
compile this closure once, preserving the bootstrap target's 64-bit identities
with JSON nodes. The closure runs on both bytecode and SLJIT with empty PATH,
unavailable C/C++ compilers, explicit instruction/deadline bounds, and captured
backend counters. Host exit zero is insufficient: the runner also requires the
exact one-result/empty-array shape, nonzero requested-backend entries and zero
other-backend/fallback/overflow counters.

This PowerShell source transport and invocation glue is temporary CI adaptation
of the current host JSON protocol, not a new test-policy implementation or a
product build dependency. Assertions and aggregation remain Chibi. No Python/C
runner, checked-in test bytecode, runtime-pin update or publication change is
required. Evidence records exact source hashes, source-map/input/compiler/runtime
and generated-fixture digests, results and counters; compiled test bytecode is
not published or promoted.

A passing pure closure does not prove a native object can be emitted, linked or
executed, nor filesystem extraction, downloads, installed SDK support, graphics,
or complete first-party migration. Those remain separate validation gates.
