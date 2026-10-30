# Formatter playground

A page for browsing the repository's Chibi sources and seeing what
`chibi fmt` would do to them with the current settings: the original on the left
(editable, or paste a source), the formatted text on the right, changed lines
marked on both sides. Indent width and tabs persist in the browser.

The page reaches its host through four calls at the bottom of `app.js`
(`list`, `read`, `format`). There is no Chibi runtime in the browser yet, so
`serve.py` is the interim host: it serves the page, lists and reads the tracked
`.chibi` files, and keeps one `chibi run apps/fmt -- --serve DIR` process
resident, passing requests through files in `DIR`. The first format waits about
25 s for that process to compile; later ones take about 0.1 s. The process ends
after 1.5 MB of source, because its garbage is collected only between runs, and
starts again on the next request.

```sh
python3 tools/docs/playground/serve.py "$PWD" "$PWD/build-release/sdk/chibi" 8765
```

Then open http://127.0.0.1:8765/. The server listens on 127.0.0.1 only and
reads only files git tracks. Replacing it means a WebAssembly build of the
formatter behind the same three calls; the page needs no other change.
