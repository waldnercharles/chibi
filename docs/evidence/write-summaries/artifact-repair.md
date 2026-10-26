# Reviewed compiler artifact alignment

The initial source commit is `884d335e8ab01a9ba644af1a65c82d8e8309bf2f`.
[Self-build run 40291027497](https://github.com/waldnercharles/chibi/actions/runs/40291027497)
passed both pinned-runtime recovery invocations on all three platforms, then
failed the scalar harness's reviewed-artifact identity check. The old seed
could recover the changed sources, but the resulting compiler and source hash
were no longer the ones recorded in the checked-in reviewed artifact.
The workflow as a whole failed; recovery alone did not satisfy that gate.

The repair follows the two steps documented for `make bootstrap-artifact`:

1. Build Release `test_language` and `chibi-compiler-host` from the clean source
   commit above with the existing Apple Clang toolchain. The standalone language
   CMake configuration uses `-O3 -DNDEBUG`, sanitizers off, `-Wl,-dead_strip`,
   `-Wno-error=string-concatenation`, and the existing third-party cache.
2. Run `language-package-compiler-chibi-bootstrap` with `CHIBI_BOOTSTRAP_SEED`
   pointing to a fresh scratch folder. It passed 23,630 assertions and retained
   stage two plus its exact compile arguments. Then run the built host with
   `--bootstrap <fresh-output> --compiler tools/compiler --core packages/core
   --seed <seed> --revision 884d335e8ab01a9ba644af1a65c82d8e8309bf2f`.
   The host validated the complete source set and target and required the seed
   to compile the sources to its own bytes before writing either reviewed file.

The generated compiler is **16,147,359 bytes**, SHA-256
`44d704f2e7d00b6241582a29ad2b1bedb8e2d966d3d3c61c21428c8e2292a037`.
It matches the initial hosted recovery output. The previous artifact was
16,146,885 bytes, SHA-256
`9ac596649812abe58a0a2a916ba5276c2eaaabdcb108fe949a17bd4082fa7e93`.

The manifest records the clean generating revision. Its only source-entry
change is `package_compiler/Compile.chibi`, from SHA-256
`b7b5c67a9135b295a988dd3da8b958611736423b562b07a62661081e28c4c681`
to `fc39dc89a0d8390d925d03d60d795cb33d2d71c9210d9036b4b0f0859c932151`.
Every source entry was checked against disk; the source list, full target,
entry, project/core roots, format, version and minimum are otherwise unchanged.

The two-loop production change, regression, and measured copy/time data are
unchanged. The scalar harness and all identity guards remain unchanged. The
runtime release pin remains unchanged; final source-only recovery uses that
checksum-verified binary, and hosted CI runs the original scalar harness.
This repair does not imply any additional performance claim.
