# Formatter playground

A page for browsing the repository's Chibi sources and seeing what `chibi fmt`
would do to them with the options you choose.

- **Code pane.** One pane, three views: Formatted (changed lines marked in the
  margin), Changes (a unified diff with unchanged runs folded) and Original.
  Prev and Next, or the `p` and `n` keys, step through the changes. Edit turns
  the pane into a text box for the original, and Scratch in the file tree starts
  from a pasted source. Show whitespace marks leading spaces and tabs.
- **Options pane.** Every option of `source_format.Options`, with what it does, a
  mark when it differs from its default, a Reset, and the current options as
  JSON. Preferred width is listed but disabled: the formatter does not wrap yet.
  The options live in `options.js`; keep them in step with
  `tools/compiler/source_format/Format.chibi`.
- **File tree.** Folders open and close; the filter opens every folder with a
  match; arrows, Home, End and Enter work as in a native tree.
- **Theme.** System, Light or Dark, remembered in the browser. Narrow windows
  slide the file tree and the options over the code.

The page reaches its host through three calls at the bottom of `app.js`
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
