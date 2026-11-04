# sdk-reflect bootstrap wrapper correction

The wrapper now stops reading bootstrap options at the first `--`. Inferred
LLVM/driver defaults are inserted after the fixed-position verb and before the
original argument list, so `sdk-reflect` receives only its original pass-through
tail. Quoted argument boundaries, empty strings, spaces and literal shell syntax
are retained. The content cache, seed integrity checks and tool-version checks
are unchanged.

First explicit values still select the bootstrap inputs. Every option occurrence,
including later ignored duplicates, now requires a nonempty value before the
delimiter. This deliberately rejects malformed duplicates and empty `--llvm`
that were previously tolerated or fell back to the environment.

Independent source review and reruns passed for the exact two file hashes in
[sdk-reflect-wrapper-results.json](sdk-reflect-wrapper-results.json):

- Shell syntax and focused whitespace checks.
- Full existing local-stub bootstrap fixture with new explicit/inferred/mixed
  options, pass-through flags, empty/quoted arguments, environment/PATH inference,
  relative paths, duplicate precedence and malformed first/duplicate values.
- Extra bare/default/unknown/single-hyphen verb, leading-empty argument and
  flag-looking duplicate-value cases.
- The original wrapper fails specifically at the new sdk-reflect argv comparison.
  Only the old census expected-order line is restored in that counterfactual
  fixture, so the earlier census case passes. A separate original-wrapper case
  selects `tail-llvm/opt` incorrectly and exits 2.

These tests use local compiler/tool/download stubs. They do not run a real native
compiler, external download, macOS or the full native Driver integration. The
change is a linear argv correctness fix, not a measured build-speed improvement.

[PR review](https://github.com/waldnercharles/chibi/pull/1331#discussion_r4443811449)
remains open until this development checkpoint is integrated and verified on
the PR's exact head. No review resolution or merge acceptance is claimed.
