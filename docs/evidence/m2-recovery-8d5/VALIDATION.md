# Frozen 8d5 sanitizer and mutation receipts

All receipts here concern source 8d5ff52831e12f624a10c178ef9d9aef072881ee,
not d459 or subsequent stage-zero integration. The disposable source view
changed only the two bootstrap inputs, as documented in README.md. No compiler
binary is included. `validation-receipt-index.json` maps every preserved raw
receipt to its exact byte count and SHA256; original absolute paths in command
records are preserved rather than rewritten.

## Mac ASan and UBSan

`sanitizer-proof/identity.json` records exact source/overlay, driver/host hashes,
67 selected instrumented compile entries, actual link commands and absent
sanitizer overrides. The preserved `compile_commands.full.json` independently
records all 505 compile entries with address,undefined instrumentation. Configuration/build commands and logs are included. The initial
sandbox configuration failure did not start compilation; its receipt is retained
separately. The successful run uses clang 23.1.1, RelWithDebInfo and explicit
address,undefined instrumentation with frame pointers. `results.json` and all
16 raw selector logs record 106 cases/95,539 assertions with no sanitizer report
or suppressions. This is Mac instrumentation coverage; it does not establish
Linux LeakSanitizer coverage or execution on the later integration source.

## Production mutation controls

`production-mutation-proof/manifest.json` records base source/fixture/header/
driver hashes, the seven exact production mutations, compilation/link commands
and every selector result. Each directory contains its production.patch,
compile/link logs and semantic test logs. The runner replaces one embedded
production Chibi component, recompiles the unchanged C fixture assertions, and
links the original inputs except for that fixture object and output. It never
regenerates a bootstrap. Compilation/linking succeeded for all seven controls;
12 selector executions failed the intended unchanged assertions. The original
fixture/header/driver hashes were checked unchanged afterward.

Controls: typed float conversion replaced with zero; untyped constant context
lost; constant sign lost; native port association lost; native evaluation order
reversed while preserving port association; array receiver watch omitted; and
floating constant bits admitted as an integer. The separate initial relative
link-path setup failure is preserved and is not counted as a semantic control.
Array control proves missing watch metadata, not live observation. The bad
native evaluation call fails emitted bytecode before its emitted-SLJIT check;
both compiler backends run, but this is not a claim of both emitted backends
executing that bad call.

Independent read-only audit verified all 73 indexed files against their preserved
originals, all 505 compile commands, both link commands and every mutation test
log. It also compared 1,551 compiler/core/src/test-language/sdk source files with
8d5 and found only the documented bootstrap input pair different. Receipt index
SHA256: f6f8944c0edd2a8b560d491f3803d4a077af68a5305ed46818cc466a06deb4ed.
