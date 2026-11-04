# Registered compiler-host construction

The fresh registered `chibi-compiler-host` target now builds at source commit
`fc6b44ea3f7e1ab75399f281a250d1520e039826`. It uses the unchanged strict GCC 14
GNU23/O3/NDEBUG recipe, including its existing warning policy, with two jobs.
No old partial-build objects were combined into this result.

All 88 C translation units compiled, 16 archives were constructed, 124 public/private
headers were staged and the host linked. Independent review matched all 105
genuine build records, 8,002 dependency occurrences over 336 paths, every archive
member against its fresh object, and the exact 424-file output census. Source,
tool, header, search-directory and Python fences remained unchanged.

The registered Graph command took 30.298866 seconds; the complete construction
phase took 35.698974 seconds inside its unchanged 180-second total limit,
including five seconds reserved for cleanup. The separate input and output
inspection phases retained their 45-second total limits. All three actual outer
exits were zero, with clean ECHILD and no errors, signals or surviving owned
children. These are correctness observations, not benchmark measurements.

## Static artifact evidence

The resulting x86-64 PIE is 1,337,048 bytes. Readelf and independent static
inspection checked its ELF structure, expected loader, lack of embedded search
paths, and 99 required strong imports against the pinned versioned libraries.
Only libm and libc are needed. Its SHA256 is
`e2b679a01af041a2077cf9ef5433063ea2ab876f1f09a9d655e26f61a94552c0`.
Exact construction identities and evidence hashes are in
[registered-host-results.json](registered-host-results.json).

## Remaining gates

This accepts construction and static artifact identity. The host was not
executed. Ordinary bytecode/SLJIT/native runtime parity, full `CTests.Run`,
broader C engine integration, actual Windows execution, exact-head CI and
whole-branch review/integration remain separate gates. The existing POSIX file
stamp precision discrepancy is not fixed by these proof fences.

Earlier failed host builds and diagnostics remain failures with their evidence
preserved. This fresh complete result does not retroactively pass them.
PR #1331 remains explicitly not ready; no PR branch, seed, protection, merge or
release was changed by this documentation checkpoint.
