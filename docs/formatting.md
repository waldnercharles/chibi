# One Chibi source-formatting policy

`fmt` lays out source, not its meaning. Project, package and standalone source
files use `authoring_source_format_with_options`; project `fmt` no longer calls
`authoring_source_canonical` (which also minimizes types/expressions). Semantic
rewrites remain explicit authoring/refactoring operations. KDL project assets
still use their own canonical savers.

## Settings and entrypoints

Put `.chibi-format` next to the project or at the repository root:

```ini
indent_width = 4
preferred_width = 0
use_tabs = false
remove_optional_semicolons = false
```

This is a small non-executable `key = value` format; blank lines, `#` comments,
and LF/CRLF are accepted. Unknown/duplicate keys, invalid values, NUL bytes,
symlinks and files larger than 4 KiB fail rather than silently falling back.
`indent_width` is 1–16; `preferred_width` is 0–4096 (0 retains expression line
breaks). Booleans are exactly `true` or `false`. The nearest config to each
source wins, with unspecified keys reset to built-in defaults, not inherited.
Discovery stops at the first `.git` boundary or filesystem root; no separate home/global
configuration or editor environment setting changes the result. `--config FILE`
selects one explicit config for all requested files.

```sh
chibi fmt --check project-or-package-directory
chibi fmt path/to/Standalone.chibi
chibi fmt --config path/to/settings path/to/source-directory
```

Source directories are walked recursively and sorted. Standalone/project/package
syntax shares the same policy; a source need not declare a package to be
formatted. Source walks include hidden folders except `.git` and `.chibi`
(repository internals and build/cache state), reject symlinks/special files and
validate all sources in each requested tree before writing any of them. An empty
source directory is a successful no-op. Project entrypoints recurse through the
project's configured content directory and retain existing project-version and
KDL validation. Repository checks below use Git's full tracked inventory instead
of folder conventions, including any explicitly tracked hidden source.

File adapters reuse the SDK Windows filesystem shim for directory walking,
canonical paths and atomic replacement; config ascent recognizes drive and UNC
roots. The buffer/options API remains platform-independent. Local validation described
in the PR is Linux-only; Windows execution is not implied by source review.

The SDK's `authoring_format_options_for_file` lets an editor resolve the same
settings before calling the existing formatter/edit-map API.
`authoring_format_file_text` is the buffer convenience entrypoint. Consumers that
supply options directly keep doing so; this change does not modify Studio's
separate editor repository. The low-level default formatter preserves every
non-whitespace token. With `remove_optional_semicolons = true`, only optional
statement/declaration terminators may additionally disappear. Required
continuation separators, comments, strings, types and expressions remain.
A reparse compares the complete syntax/field/token structure with only optional
terminators ignored. Invalid source is refused, not repaired. The check also
keeps formatting idempotent; comments and literal contents remain byte-exact.

The repository keeps existing semicolons and expression wraps for this rollout.
Projects can opt into removing optional semicolons without invoking semantic
minimization. Changing the repository default is a separate, reviewable corpus
normalization, not hidden churn in this implementation.

## Humans and ephemeral cloud agents

```sh
make format-chibi          # format all tracked authored .chibi files
make check-format-chibi    # read-only check + second-pass idempotence
make format               # Chibi plus existing C formatting
make check-format         # both read-only checks
make install-hooks        # explicit local opt-in, never run by configure
```

The repository command builds only syntax + the shared native formatter, not the
engine, SDK, renderer, compiler runtime seeds or test suite. It needs Python 3,
Git, a C11 compiler, and the existing pinned tree-sitter source. It never installs
or downloads prerequisites. Point `CHIBI_TREE_SITTER_SOURCE` at an already
provisioned clean checkout of revision
`da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`, or provision that checkout at
`.chibi/tree-sitter` in your development/CI environment. Existing
`build{,-debug}/_deps/chibi_language_tree_sitter-src` checkouts are also found.
`CC` selects the compiler. Build inputs and compiler identity invalidate the
small local `.chibi/format` cache; the tree-sitter pin must agree with the main
third-party manifest. A missing prerequisite is an explicit failed check.
The source-only workflow provisions that exact dependency with checkout, without
package installation or a full native build.

The opt-in hook formats the **complete staged Chibi snapshot**, including staged
configs, and re-stages only necessary authored-source replacements. A file that
needs rewriting but has unstaged changes is refused, never overwritten; already
canonical partially staged files remain untouched. If formatter implementation
itself has unstaged edits, stage/stash them first: formatting index sources with a
different formatter would not establish the commit's policy. The separate
`python3 tools/chibi_format.py --staged` command remains read-only. Existing C hook
behavior is retained afterwards. Setup refuses a custom `core.hooksPath` or an
existing `.git/hooks/pre-commit`; integrate the repository hook manually instead
of overwriting personal hooks. It never changes global Git settings, and configure
does not install hooks. A clone alone does **not** activate hooks; local hooks can
also be bypassed.
Writes are atomic per file, not a crash-atomic multi-file transaction or a lock
on external editors. Detected races abort and restore only unchanged formatter
writes. Clean/smudge filters or CRLF conversion that make worktree bytes differ
from staged bytes are conservatively refused when a rewrite is needed.

## PR and push checks; automatic repair follow-up

The lightweight `Chibi formatting` workflow checks every PR and branch-push
source head, including cloud/API publication, coalescing overlapping runs for the
same SHA. It is **read-only**: this checkpoint does not append automatic formatting
commits to PR branches. Automatic server-side repair is separate follow-up work;
the opt-in local hook does not fulfill that cloud/API requirement.

No editor integration is needed for the shared command or CI check. Local hooks
cannot intercept GitHub API publication, a clone does not install hooks, and CI
checks source heads rather than every intermediate historical/local commit.
Required server checks and branch protections remain separately configured by
repository maintainers. This change neither reconfigures them nor expands the
full-engine workflow to every branch push.

## Exact fixtures and generated sources

Every tracked `.chibi` path is included, including `tools/compiler`, tests,
toolchain/build graph code and nested content. There is no blanket test-folder
or compiler-folder exclusion and no grandfathered formatting baseline.
`tools/format/exceptions.json` permits only explicit existing paths:

- The two original Studio version-23 migration inputs are SHA-256 pinned, point
  to `test/tools/project_declarations.c`, and must still fail current syntax
  formatting. Changing their bytes, removing the owner test, making them valid,
  or leaving a stale exception fails the check. They are never rewritten.
- Generated sources must name their generator's non-mutating consistency-check
  command and a reason. The command runs in an isolated copy of tracked inputs and must reproduce/verify committed output;
  failure or any tracked input/output mutation in the isolated snapshot fails the check. Generated output must also pass the shared formatter check. `--write` never hand-formats
  generated files: fix the generator and regenerate. There are currently no
  tracked generated `.chibi` outputs, so this map is empty. The runtime-generated
  build/cache tree is not a substitute for the tracked authored inventory.

A formatter error in any authored file prevents a repository-wide write. The
read-only staged check creates an isolated index snapshot; generated entries
materialize the complete tracked snapshot so generator checks see the same
committed inputs. Run filesystem/enforcement regressions with
`python3 tools/test_policy.py tool chibi-format`. Syntax/options/maps and semantic
minimizer separation remain in the existing `test_language --format` dispatcher.

## Relevant prior art

- [Prettier configuration](https://prettier.io/docs/configuration): per-file upward
  discovery, no global configuration, options shared with its API. Chibi uses the
  same reproducibility principles but one deliberately small, non-executable
  config format and a repository boundary.
- [clang-format](https://clang.llvm.org/docs/ClangFormatStyleOptions.html): nearest
  parent style file plus an explicit file override. Chibi similarly resolves
  independently for each input; it does not inherit a large C++ style surface.
- Odin's [OLS/odinfmt](https://github.com/DanielGavin/ols) has an
  [odinfmt.json schema](https://github.com/DanielGavin/ols/blob/master/misc/odinfmt.schema.json)
  with width, indentation and tab controls. Its additional import sorting and
  syntactic conversion policies are not adopted implicitly by Chibi's formatter.

This adds no language syntax. Semantic-rewrite proposals remain separate design work.
