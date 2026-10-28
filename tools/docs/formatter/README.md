# Browser formatter

This is a thin browser adapter over `authoring_source_format_with_options`, not
a JavaScript formatter. It compiles the same syntax snapshots, generated parser,
Tree-sitter runtime and formatting implementation used by `chibi fmt`. Source is
parsed, formatted and checked for syntax equivalence; it is never executed.

The worker owns its WebAssembly memory. Input is bounded to 256 KiB, output retains
the formatter's native limit, and the page terminates a formatting request after
three seconds. No source upload or automatic persistence is implemented. A
versioned URL fragment can share all four settings; including source is opt-in.

## Checks and build

Use Python 3.11 or newer. Provision the repository-pinned Tree-sitter source and
Emscripten 6.0.10 before running these commands. The scripts do not download or
install a toolchain:

```sh
python3 tools/docs/check.py --runtime /path/to/tree-sitter
python3 tools/docs/check.py --runtime /path/to/tree-sitter --sanitize
python3 tools/docs/formatter/build.py --runtime /path/to/tree-sitter --output /tmp/formatter
```

The small native check uses the existing C/pico_unit tools dispatcher, running
`--docs-website` and `--formatter-web` from one temporary executable. It exercises
real packaging and the browser C boundary; it is not browser execution or a full
engine test pass. The output directory for a browser build must not exist. Release
builds require a clean committed engine tree and a clean exact Tree-sitter tree.
Generated `engine.json` records the revision and JavaScript/WASM checksums.

The documentation workflow combines `chibi doc` and that exact formatter build.
Pull requests build without deploying. A manual run deploys only with
`deploy=true` on `main`, subject
to the existing GitHub Pages environment protections. Missing or inconsistent
inputs fail the composition; there is no placeholder or fallback formatter.

Browser acceptance must still cover native CLI parity, rapid edits, worker
failures/retry, exports, share-link validation, and keyboard/mobile use before the
first production deployment.
