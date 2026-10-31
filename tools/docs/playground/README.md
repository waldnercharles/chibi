# Formatter playground

A page for browsing the repository's Chibi sources and seeing what `chibi fmt`
would do to them with the options you choose. It uses the documentation
site's theme (`../theme.css`: its colors, Geist and JetBrains Mono, and the
GitHub code palette) and its highlighter, `tools/compiler/source_highlight`,
which colors code by its parse tree.

- **Code.** One pane, three views: Formatted (changed lines marked in the
  margin), Changes (a unified diff with unchanged runs folded) and Original.
  Previous and Next, or `p` and `n`, step through the changes. Edit turns the
  pane into a text box for the original; Scratch in the file tree starts from
  a pasted source. Whitespace marks leading spaces and tabs.
- **Options.** Every option of `source_format.Options` in groups. Hovering or
  focusing an option's name explains it and names the same setting in other
  formatters. Changed options are marked, Reset restores the defaults, and the
  pane shows the changes as a `.chibi-format` file for `chibi fmt`. Options the
  formatter does not have yet are listed as planned. They live in
  `options.js`; keep it in step with `tools/compiler/source_format/Options.chibi`.
- **Files.** Folders open and close; the filter opens every folder with a
  match; arrows, Home, End and Enter work as in a native tree.
- **Theme.** Auto, Light or Dark, remembered in the browser. Narrow windows
  slide the files and the options over the code.

The page reaches its host through three calls at the bottom of `app.js`
(`list`, `read`, `format`). There is no Chibi runtime in the browser yet, so
`serve.py` is the interim host. It serves the page, lists and reads the tracked
`.chibi` files, and keeps `host/Main.chibi` running as a program assembled with
the packages it imports, passing requests through files in a mailbox folder. One
parse of each request serves the formatting and both highlighted texts. The
first format waits about 30 s while the program compiles; later ones take about
0.1 s. The program ends after 1.5 MB of source, because its garbage is
collected only between runs, and starts again on the next request from the
closure the launcher kept. It cannot reuse the formatter the launcher embeds
for `chibi fmt`, which formats files and has no highlighter; the formatter, the
parser and the highlighter together take most of the budget a program compiled
from source may use.

```sh
python3 tools/docs/playground/serve.py "$PWD" "$PWD/build-release/sdk/chibi" 8765
```

Then open http://127.0.0.1:8765/. The server listens on 127.0.0.1 only and
reads only files git tracks. Fonts load from the Fontsource CDN unless installed.
Replacing the server means a WebAssembly build of the formatter behind the same
three calls; the page needs no other change.
