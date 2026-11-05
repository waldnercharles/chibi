# CLI terminal failure and C preprocessing validation

## CLI result: failed, preserved

The complete authored `test/native_cli/survivors.sh` run used source `a26027facc2c0de6e92abc71a2641186da730544`, tree `c8afcdc07a07f9983a4f56b95f95cd44b4e9faf3`, and the current accepted native compiler described in [the product report](NATIVE-PRODUCT-VALIDATION.md). Sixteen fresh controls passed independent review first, including a real retained-PTY control. Four positive controls passed; twelve intentional failures rejected acceptance. All 113 known inert executable placeholders were quarantined to 0600.

The real run exited **1 after 130.800 seconds**. The complete ChibiCliTests program returned zero before foreground tests began. Both foreground fixture compilations, four scope-signal invocations, all four parent-only signal/descendant cases, and PTY cases 0 and 1 passed.

PTY case 2 failed the existing assertion that the CLI finish within five seconds after Ctrl-C. Before that failure, its readiness, pre-stop echo, stopped CLI, terminal reclaim, resumed echo and foreground-regain assertions passed. Cases 3–5 were never reached. The full CLI gate therefore remains failed. No timeout was extended, assertion removed, or failed attempt reclassified.

The failure occurred well before the unchanged 930-second owner bound (900 seconds work including preparation, 30 seconds cleanup). All 14 adopted descendants were reaped, with ECHILD and pipe EOF, no survivors, owner signals or owner errors. Four remaining executable/copy targets were quarantined to 0600. Source/product bytes and modes and the original supplier's exact accepted stat identity remained unchanged. The worker did not save a success result, so its in-memory pre-suite private metadata snapshots are unavailable. Cleanup receipt matching without worker success evidence does not establish full successful-output acceptance.

A second wrapper stop around foreground group continuation is a plausible source-level explanation, not a demonstrated cause. The retained data does not show process state at the assertion deadline. Failure-only state diagnostics should establish that state before changing production wait behavior. The five-second assertion, all six PTY cases, cancellation behavior and independent-child stop propagation must remain intact.

Failure review SHA256: `6575e4e292588167f9d53d3e1c9d4ccff2dc0dcfe44914dbd1a7ddef7cb9f5a4`. Controls acceptance: `fd6d5b365dfd8c3fac97c1be63ca066a44eb1ddbbaa1eb1444bf44339bf326dd`.

## C preflight: three fresh stages accepted

These stages use frozen source `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`. Twelve fresh positive/negative controls passed independent review. Each actual stage was separately admitted and observed, with an unchanged 20-second total and five-second cleanup reserve.

- GNU23 host built-ins and ordered include search passed in 0.467 seconds. The exact pinned compiler is GCC 14.2.0. Its experimental C23 mode reports `__STDC_VERSION__=202000L`; this is metadata, not proof that every C23 feature needed by the source compiles.
- C11 pthread bridge driver print-only validation passed in 0.568 seconds. Exact ordered cc1/as/collect2 commands matched the pinned driver; those printed tools were not executed in this stage.
- Real C11 bridge preprocessing passed in 0.606 seconds (native command 0.041 seconds). Independent review verified 931 macros, strict ANSI/C11 `201112L`, pthread/POSIX/Darwin feature definitions, absence of Release-only NDEBUG/optimization macros, five ordered include directories and all 67 active dependency-file pins. The private dependency file and complete output layout passed.

All three stages passed terminal, ownership, cleanup, immutable input and fresh control/dependency bindings. These results do not establish C compilation, assembly, linking, asset-bridge runtime or the full host's active headers.

Two earlier attempts remain failed and preserved. The old host validator expected the final C23 metadata value instead of the pinned GCC14 experimental value. The old dry-command validator expected split `-D _REENTRANT` and omitted the assembler's actual `-I` operand. The fresh revision corrected only exact pinned-tool expectations, dependent positional checks and fresh attempt bindings; authored compiler flags and assertions were not weakened. The old captures were not revalidated into passes.

Host acceptance: `28e9106c20dda8bc6eba13bf0034a51939e2f5f326dc38cfc5e2958804ef8021`. Dry-command acceptance: `4ec5d2b773814dada7121843f3d3c023c4fccbe4bd7063e33f5c96e88b60dc58`. Preprocessing acceptance: `62a8e4d3633aa54e1f4a1eacefb9eb85df758407d87795ba9cad4a1003c87450`.

## Remaining gates

The [current compiler fixed-point, 573 tests and archive invalidation](NATIVE-FOLLOWUP-VALIDATION.md) retain their earlier scoped acceptance. The CLI failure, held SDK installation/relocation, real C host and asset-bridge execution, ordinary VM/SLJIT/native differential, actual Windows execution, and exact-head integration/review remain open. The ordinary Linux Windows-artifact producer includes the excluded broad pointer corpus; full mode also includes the held SDK gate. It must not be used to route around those holds, and the existing Windows-only workflow has no targeted-suite selector.

PR #1331, the terminal-unknown c5 bootstrap and excluded native 16-byte union investigation remain held. Stronger per-Capture descendant proof remains failed/unproven. No merge or release readiness is claimed. The adjacent JSON records exact identities, timings and limitations without publishing local binaries or private proof archives.
