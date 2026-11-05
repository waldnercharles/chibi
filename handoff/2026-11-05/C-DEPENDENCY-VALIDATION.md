# Remaining C dependencies and driver commands validated

This checkpoint extends [the single-translation-unit report](TEXT-C-VALIDATION.md). It establishes dependency preprocessing for the remaining 87 C translation units and two strict GCC driver `-###` print-only checks, **not compilation of those 87 objects**. Frozen source remains `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`.

## New accepted evidence

- The fresh r4 stage passed independent review within the configured checks described below: all 87 dependency queries and both driver print-only commands completed. Outer exit was zero in 4.103953681 seconds.
- The profile is Linux x86-64, GCC 14.2, with original GNU23 Release flags, 17 include layouts and three flag classes: 41 ordinary, two vendor and 44 execution translation units. Authored compiler flags and commands were unchanged.
- Validation covered 335 unique active input identities and hashes, 302 staged source/header files and 87 inert dependency files. No object or temporary outputs remained. Configured cleanup, staged-input and runtime-integrity checks passed, including whole-owner ECHILD and EOF checks; the runtime checks had the loader-alias coverage gap below. Stronger per-Capture descendant proof remains unproven.
- All 21 fresh positive/negative controls were accepted. Separately, 344 pure validation checks passed; these are validation-source checks, not Chibi product tests.

The dependency queries perform preprocessing only. The two print-only commands validate exact ordered compiler and assembler arguments; their printed `cc1` and `as` rows do not mean those tools executed during the dry commands. The prior accepted compilation of `src/types/text.c` remains separate.

## Runtime coverage qualification

A later audit found that r4 pinned the resolved library bytes but omitted the loader-facing `libexpat.so.1` and `libffi.so.8` alias chains from its runtime checks. These are validation-runtime libraries, not additional C source dependencies. The 87 dependency maps, two ordered dry-run arrays, 89 recorded captures and configured containment/input/runtime-check outcomes remain genuine recorded evidence. No alias drift was observed, but that does not prove historical alias immutability: the omitted checks were not performed, and complete recursive loader-alias assurance was not established.

This qualification preserves the recorded results and control outcomes without retrospectively reclassifying them. Future compilation must freshly check the corrected loader-alias chains as well as bind the source, staging, flags and input identities.

## Preserved failures and corrections

Dependency validation r2 and r3 remain failed runs. R2 lacked the canonical spelling of an already pinned errno header; explicit aliases corrected that validation gap. R3's expected assembler arguments omitted `-W`, which GCC forwards from the original vendor `-w` flag. The fresh r4 expectation includes that exact token in its required position. Neither correction changed authored compiler flags or commands, relaxed ordering, or reclassified an old failed run as a pass.

The adjacent [JSON](c-dependency-validation-results.json) records the accepted scope and unrun stages. Any later object compilation must freshly bind source, staging, flags and input identities rather than treating this checkpoint as compilation acceptance.

## Unchanged limitations

The remaining 87 object compilations, 16 archives, host link/runtime and Windows validation remain unpassed. This checkpoint proves no full new-head integration or new CLI/SDK pass. CLI r3 remains failed and preserved; CLI r4 and SDK work remain held without reruns. PR #1331, terminal-unknown c5 bootstrap and the native 16-byte union investigation remain held. Existing branch and PR holds are unchanged; no merge or release readiness is established.

This is a documentation-only checkpoint with no product-source changes, binaries or private proof archives.
