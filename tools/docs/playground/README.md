# Formatter playground

The playground runs the shared Chibi formatter, parser and highlighter in a
WebAssembly worker. It accepts up to 400,000 UTF-8 source bytes, formats and
highlights them, and shows the original, formatted result and changes. Source
stays in the browser. Type styles use compiler analysis without launching the
user's program.

## Build and run

Use a native Chibi compiler with the `wasm32-unknown-emscripten` LLVM target,
native LLVM tools, Emscripten, and the LLVM tools from that same Emscripten
installation. The build validates the toolchain and installs nothing.

From the engine checkout:

```sh
sh tools/docs/playground/build.sh \
  /path/to/chibi /path/to/native/llvm/bin \
  /path/to/emcc /path/to/emscripten/llvm/bin \
  /tmp/formatter-site
python3 -m http.server 8765 --bind 127.0.0.1 --directory /tmp/formatter-site
```

Open `http://127.0.0.1:8765/`. The output directory must be new. Failed builds
leave existing output untouched. `build.json` records compiler, source/tool
and artifact hashes; `files.json` lists the Git-tracked Chibi examples and
their hashes. Untracked scratch files are excluded.

The same directory can be published under the documentation site's
`formatter/` path. Every asset and example URL is relative to the page, so
GitHub Pages needs only static hosting. Publishing must include the whole
directory, including `wasm/`, `sources/` and `files.json`. No server API,
threads or cross-origin isolation are required.

The earlier 2026-11-02 local preview of the 174-setting build is available at
[127.0.0.1:8768/chibi/formatter/](http://127.0.0.1:8768/chibi/formatter/), served
from `.chibi/formatter-full-final-reflow`. The older services on ports 8766 and
8767 were left untouched. That artifact predates the SEGMENT replacement and
the current 173-setting catalog. GitHub Pages has not been deployed.

## Style controls

The [canonical catalog](../../compiler/source_format/options.json) defines 173
settings. The [coverage matrix](../../../docs/formatter-implementation.md) maps
every survey family to working controls and identifies unsupported variants.
The panel has searchable groups and versioned current, compact, expanded and
tabular profiles. Explicit overrides win over the selected profile.

Examples of independently configurable styles include:

- Foreign and `when` block indentation, brace placement and padding by construct,
  short-body policies, brace presence, unbraced-body placement and following-`else`
  placement.
- Block or `=>` function bodies, showing or omitting optional `fn`, and showing
  or omitting the return-type `->` marker. Required disambiguation remains.
- Explicit, redundant-annotation omission and evident-type preferences for
  locals, constants, default parameters, inferred results and supported members,
  with a public-API override and alias-spelling policies.
- List packing, vertical lists, per-context widths, binary/chain breaks,
  operator-family spacing, delimiter/colon spacing and bounded column alignment.
- Logical code paragraphs, syntax-based blank lines, standalone comment-group
  gaps and preserved or aligned data-table columns.
- Checked parentheses including mixed-operator groups, per-list trailing commas,
  imports, numeric spelling, string/char escapes and opt-in prose/doc comment reflow.
- Declaration or statement line selection that expands the necessary context
  and preserves bytes outside it.

For example, these exported overrides select the requested function syntax,
indent declaration blocks and enable inferred logical sections:

```text
indent_foreign_blocks=true
indent_when_blocks=true
function_body=expression
function_keyword=always
return_arrow=omit
local_types=omit_redundant
statement_paragraphs=logical
```

Defaults preserve source type annotations and syntax preferences. Semantic
styles use the compiler's actual types and prepared representation. They
preserve operations, conversions, effects, signatures, exports and lifetimes.
Narrow numeric context, required parameter/field types and unresolved or
unsupported sites remain with notes. Generic definitions are not specialized
from one concrete call.

`analysis_target` chooses the initial compiler analysis and candidate type
spelling. `semantic_targets=all` is the default: compiler-backed edits must
preserve meaning on Linux, macOS, Windows and WebAssembly. A failed proof keeps
the affected source with notes while layout remains available.
`semantic_targets=selected` explicitly limits proof to the selected target.
The lower-level `source_type_style.RewriteWith` API retains its selected-target
contract; the public formatter adds the all-platform check.

For tracked repository files, `project-context.mjs` loads the package and its
dependency sources from the static source inventory when a semantic style needs
them. It sends at most 256 documents and 16 MiB to the worker. Scratch documents
can still format without project context. Missing imports or incomplete drafts
keep semantic changes unapplied while ordinary layout remains available.
`source_format.FormatProject` provides the same pure, filesystem-free API to
native callers with dependency documents.

`statement_paragraphs=preserve|syntax|logical` keeps authored sections, adds
deterministic structural gaps, or runs SEGMENT, respectively. Logical groups
use the published four phases: identify action units, resolve overlaps, refine
same-syntax units with sliding triples, and merge compatible singleton units.
Its identifier and RHS rules determine categorical similarity levels. Nested
bodies are analyzed separately when they have at least four immediate statements.
Authored blanks, comments and protected regions remain in place. The
[SEGMENT reference](../../../docs/formatter-segment-algorithm.md) documents the
published algorithm and the implementation choices required by gaps in the
recovered source. The [research note](../../../docs/formatter-readability-research.md)
describes the human evaluation that remains to be done.

## Configuration interchange

Select **Chibi** or **EditorConfig** in the configuration panel to choose the
export format. To import, paste into the separate editable box and select
**Apply config**. Invalid recognized settings leave the current configuration
unchanged and show diagnostics. Unsupported keys produce warnings.

EditorConfig supports common indentation, tab-width, line-ending, final-newline
and maximum-line-length settings. `chibi_profile` and `chibi_*` keys cover the
complete Chibi catalog, with custom keys overriding their common equivalents.
Later matching sections win. Unspecified values keep the current settings unless
a profile supplies a new baseline. Matching uses repository-relative paths;
Scratch is `scratch.chibi`. Import reads one pasted file and does not discover
ancestor configs. The [configuration reference](../../../docs/formatter-implementation.md#configuration-import-and-export)
describes glob support and precedence.

## Implementation

`wasm/Main.chibi` calls `source_format` and `source_highlight` over one parse for
ordinary layout. Compiler-backed styles reuse the compiler's declaration,
specialization and preparation passes. A changed stage gets a matching parse;
token-changing styles reclassify the final output for highlighting. Successful
responses include skipped-style and analysis notes.
Comment reflow runs after final indentation, so its width calculation includes
the output margin and stays stable after one formatting pass. Range selection
then extracts the completed result.
Its C boundary uses `runtime/program_wasm`, shared with the browser asset
compiler, for per-call allocations, quotas and failure cleanup. `worker.mjs`
loads the compiled module; `client.mjs` keeps one active request and the latest
waiting edit. Worker failures and timeouts allow Retry to start a new worker.

Edit `tools/compiler/source_format/options.json`, then run
`node tools/docs/playground/generate-options.mjs` to regenerate
`Options.chibi` and the UI's `options.js`. The generator's `--check` mode detects
stale output. The configuration panel exports changed options as `NAME=VALUE`
lines.

## Checks

```sh
node tools/docs/playground/generate-options.mjs --check
node --test test/docs/playground.mjs test/docs/options-catalog.mjs \
  test/docs/project-context.mjs test/docs/playground-config.mjs
CHIBI_FORMATTER_MODULE=/tmp/formatter-site/wasm/engine.mjs \
  node --test test/docs/formatter-wasm.mjs
```

Also run the authored `source_format/tests` and `source_highlight/tests`
packages with the native compiler. Changes to the shared runtime require
`test/native_aot/asset_compiler/wasm.sh`, which checks plain and threaded
execution, quota/cancellation recovery, allocation cleanup and symbol isolation.

Browser acceptance covers the site under a URL prefix, a tracked source file,
scratch edits, option changes, invalid-source correction and rapid edits.
Use a static server with no `/api/format` route to verify browser execution.

Include this file-edit regression in manual acceptance:

1. Open a tracked file with imports, such as Bytes, and enable a compiler-backed
   type style. Confirm that repository dependency context resolves.
2. Edit the source. The filename should remain visible with `, edited`, and the
   next semantic request should still resolve its imports.
3. Import an EditorConfig section scoped to that exact repository-relative
   filename. Confirm that it matches the edited document and applies its options.
4. Try an invalid recognized config value. Confirm that diagnostics appear and
   the active settings remain unchanged.
5. Open Scratch explicitly. Confirm that its file identity is cleared and
   section matching uses `scratch.chibi`.
