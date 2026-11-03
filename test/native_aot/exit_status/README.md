# Native Main exit status

`sh test/native_aot/exit_status/run.sh /path/to/chibi /path/to/llvm` compiles the
actual `native_link.Start` source into a small emitter. It verifies seven
platform/context modules and substitutes that emitted startup IR through the
normal LLVM pipeline. The same CLI/built-executable cases are also called from
`test/native_cli/run.sh` against the supplied compiler's own startup directly.

At O0/O2, plain and context programs keep valid results 0, 134 and 255. Results
-1 and 256 report the existing Main/0..255 diagnostic and exit 1. A separate
context-failure stub returns 256 while setting the failure slot; it must still
exit 134 without the range diagnostic. This does not exercise shared entries,
and cross-target IR verification is not a Windows execution claim.
