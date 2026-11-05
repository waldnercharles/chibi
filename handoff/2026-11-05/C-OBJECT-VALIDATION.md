# Remaining C objects validated

This checkpoint extends [dependency and driver validation](C-DEPENDENCY-VALIDATION.md). The fresh r4 run passed independent actual-artifact review for all 87 remaining C compilation commands and their object/dependency pairs. Acceptance covers compilation, structural ELF validation and ordered dependencies. Frozen product source remains `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`.

## New accepted evidence

- Linux x86-64, GCC 14.2, original GNU23 Release flags and command ordering: 17 include layouts, 41 ordinary, two vendor and 44 execution translation units. Product source and authored compiler flags were unchanged.
- All 87 commands exited zero with no captured diagnostics, producing 87 inert object/dependency pairs: 2,035,368 object bytes and 545,527 dependency bytes. The outer process exited zero in 50.495719516 seconds; the owner completed in 50.174908204 seconds. Whole-owner ECHILD and EOF checks passed, with no errors, signals or remaining children.
- Fresh checks bound 302 staged source/header files and 335 unique active input identities. Loader-facing alias checks and all 174 captured/pre-finalization/post-finalization output identity associations passed, including the final capture boundary.
- All 38 fresh positive/negative harness controls passed independent review. Separately, 383 pure harness checks and the generic ELF parser's 101 prior, 15 new alignment and seven independent actual-header regression cases were reviewed. These are validation-harness checks, not Chibi product tests; overlapping coverage is not a combined test total.

Objects remain inert. Generic ELF structural validation establishes neither API nor runtime correctness. The prior accepted `src/types/text.c` object remains a [separate checkpoint](TEXT-C-VALIDATION.md), not an integrated build. The adjacent [JSON](c-object-validation-results.json) records accepted scope, exact timing and evidence digests.

## Preserved failures and corrections

Dependency r4 omitted historical checks of the loader-facing `libexpat.so.1` and `libffi.so.8` alias chains. Fresh compilation checks those chains; it does not prove their historical immutability or revise the earlier qualification.

Compile r2 remains failed: its replacement-output control did not demonstrate the intended replacement, and durable output identity coverage had a gap. Fresh r4 controls exercise genuine replacement and mutation at the final capture boundary. Compile r3 remains failed at command index 3: the validator rejected valid `.rodata.cst16` bytes in `type.c.o` because it incorrectly required generic file-offset alignment.

The narrow parser correction follows the [ELF gABI section-header rule](https://gabi.xinuos.com/elf/03-sheader.html): `sh_addralign` constrains the section address, not a generic file offset. It removes that unsupported offset check while retaining bounds and the other structural constraints. No product source or authored GCC flags changed. Earlier failed attempts remain failed.

## Unchanged limitations

No archives, linking or generated-object execution occurred. The 16 archives, C-host link/runtime, Windows validation, full new-head integration and new CLI/SDK pass remain unproved. CLI r3 remains failed and preserved; CLI r4 and SDK work remain held. PR #1331, terminal-unknown c5 bootstrap and the excluded native 16-byte union/corpus work remain held. Stronger per-Capture descendant proof remains unproven; the supported containment scope is whole-owner ECHILD/EOF. Official libdep package provenance remains unresolved.

This documentation-only checkpoint contains no binaries or private proof archives. Existing branch and PR holds remain unchanged; no merge or release readiness is established.
