# Prepared negative controls — not executed

Parent must release the build lane and explicitly authorize execution first. Current positive evidence is the parent's unchanged dispatcher model: 1 case / 261 assertions / 6.152 seconds, binary `71d45f066d17fcf686906a71fde7e625bfaa3f2eb854a1d78c33139267903344`, SDK build `c676e3cc48e8a3fb2110d57477d5d83875a0ac8b337d4e549129eeb3caf274d9`. `plan.json` records the exact package, compiler, source, fixture, main-object, Studio archive and build-recipe hashes. README-only updates after the source freeze do not affect these controls.

`run.py` is a prepared executable recipe, not a result. It has not been run or syntax-checked. After authorization:

```sh
python3 /Users/cwaldner/Personal/chibi/.worktrees/typed-assets/chibi/.chibi/asset-main-integration/studio-controls/run.py --execute
```

Use `--control ID` to select one or more controls. Default is six sequential controls:

| Control | Only old behavior restored | Required existing failure |
| --- | --- | --- |
| graph-codec-failure | Producer ignores rejected document payload | `!compiled_collab_action` after 1M invalid bytes |
| graph-type-name | Decoder omits declared type name | Owned default `type_name` after temporary retirement |
| history-byte-decoder | PIN_STR history decoder accepts only JSON strings | Non-UTF8 checkpoint round trip |
| checkpoint-before | Restore skips native representation check on before side | Native-NUL before checkpoint refusal |
| checkpoint-after | Restore skips native representation check on after side | Native-NUL after checkpoint refusal |
| field-batch-preflight | Replay checks types but not native representation | Earlier Transform field must remain unchanged |

Each control uses the existing production acceptance dispatcher and unchanged tests. A Clang VFS overlay replaces one production C translation unit; original source paths and include roots remain intact. Its exact compile-database command is reused with scratch output/dependency paths. One object replaces the same unique archive member in a scratch COW copy of `libstudio.a`. The existing acceptance link command is reused with that archive and a scratch output. No generated CMake files, SDK files, resource manifests, active source files or index entries change.

Scratch binaries live in this directory's `bin/`; `share/chibi` links to the accepted build's resources so executable-relative discovery remains identical. Runs use the existing Python native policy and `--compiled-asset-edit-model`, with persistent asset cache disabled. There is no new selector, test case, application type, compiler workload, project fixture or rendered frame. Each unchanged model invocation still performs its existing temporary project/setup work.

A control counts only if the process fails at its named existing assertion. Compilation, linking, resource/setup errors, unexpected signal termination, or an unexpected pass are not fail-before evidence. Record elapsed times through the policy; impose no universal wall-clock deadline. Receipts record mutated source, compiler/link commands, baseline/control binary hashes, exit status and expected failure match. Generated control binaries/archive/object files are deleted after their expected failure is confirmed; receipts, overlay source and logs remain in this one directory. Unexpected failures retain their scratch artifacts for inspection.

The active index diff and status are compared before/after execution. Parent should inspect the recipe before authorizing it; no control execution occurred during source review or plan preparation. The future Studio92 candidate needs a README refresh for the newly recorded positive pass before use; its prior source review remains applicable to unchanged production and fixtures.
