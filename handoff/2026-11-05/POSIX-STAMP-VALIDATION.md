# Full-width POSIX stamp validation

The complete unchanged `test/native_aot/posix_stamp/run.sh` passed on Linux x86-64 at source `1f2e912b617e3cd6e832e3cbdabc04076a4ef695`, after the full-width stamp correction and actual C-host oracle were published. Independent review accepted the real result, not only its execution plan.

## Actual coverage

- All authored synthetic word, signed/unsigned decimal, octal, nanosecond, input-preservation and repeated-stamp checks passed at native O0 and O2. This executes the Linux target layout; the authored Darwin branch was not executed.
- One strict C build used the production `build_io_stamp` and allocator. Four jobs-1 Chibi builds produced the two synthetic programs and two public stamp probes.
- Both C and native probes ran twice per optimization level over seven actual paths. Complete bytes matched Python's integer-nanosecond stat oracle: a file, empty file, directory and followed symlink have stamps; missing, broken-link and ENOTDIR paths return empty strings. Final metadata stability and file contents were also checked.
- The exact two authored PASS lines were the entire 114-byte stdout. Stderr was empty. The shell, worker and observed outer process exited zero; ownership reached ECHILD and EOF without signals, errors, cancellation or surviving children.

The complete owned suite took 5.625 seconds inside its original 180-second total limit, including its ten-second cleanup reserve, with affinity [0,1]. This is a correctness observation, not a quiet benchmark. Source/tool/runtime checks passed before and after execution and again independently: 508 file facts, 30 directory inventories, 714 required absences and all 4,172 source entries. Private cwd, home, temporary and cache directories were empty afterward. The original accepted compiler and private copy remained identical and non-executable after use.

The fixture's own cleanup removed its temporary executables and files. The preserved proof is the exact source, compiler identity, actual streams, launch and terminal records, ownership/cleanup evidence and independent review. It does not claim separately retained per-probe binaries.

## Preservation and remaining gates

The locally verified source/proof archive is 23,150,196 bytes with 4,216 members, SHA256 `533de6d7e3f815469feec6bb9f55d6472843dcea56870a26fd65a7b786387aef`. Every member and live input was rechecked. Packaging performed no upload or generated-program execution. Exact source, fixture, controller, receipt and acceptance identities are in [posix-stamp-results.json](posix-stamp-results.json).

This closes the focused Linux POSIX stamp regression. Darwin/Windows execution and changed-source Graph, Driver, product and broader integration gates remain separate. The unknown original c5 bootstrap, excluded native union leak investigation and PR #1331's explicit hold are unchanged. No merge, release or whole-branch acceptance is implied.
