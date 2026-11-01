# Handoff: old-formats (2026-11-02)
- Branch `cwaldner/scalar-producer-tests` holds the WIP draft PR waldnercharles/chibi#1322; last pushed commit 34d6312b2. Every earlier branch of mine is merged: #1226, #1243, #1090, #1300, #1304, #1307.
- Done:
  - C test verdicts and VM deletion list: docs/c-test-verdicts.md, handoffs/vm-deletion.md §5;
  - language-rule samples;
  - test/native_aot/os (native core:os);
  - BuildsTheWorkloads;
  - test/graph/ChibiCliTests.chibi (row chibi-cli-authored).
- In flight: nothing running, no CI of mine pending. The scalar-producer port was stopped mid-way. Its draft holds only tools/compiler/package_compiler/tests/Probe.chibi, with the linux-x64 target description as data; it is unverified, and the C cases are not deleted yet.
- Next steps:
  1. Finish the scalar-producer tables: the row kinds per source and the Validate rejection messages, as in the C file's MALFORMED_* tables.
  2. Move test/scalar_facts/cases where tests/ can import it.
  3. Delete the 8 C cases and their helpers.
  4. Run `chibi test tools/compiler` and the language-package-compiler-chibi-scalar row.
- After that, gate 5: delete the packaged package compiler with lang-ux's native `chibi` (gate 4). I asked lang-ux whether it goes in their PR or right after it.
- Local only, none of it matters:
  - /tmp/macos-native/llvm, symlinks to Homebrew LLVM 22 for CHIBI_LLVM_BIN;
  - /tmp/nat-*, /tmp/sh-*, /tmp/os-*, /tmp/wi-*, scratch logs and projects.
