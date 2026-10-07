Fresh browser acceptance passed at `cac4acb9d78cb3f8dd0b669a7b0e3f80a4141216`.
Normal `make web JOBS=4` exited 0 after rebuilding both serial and worker variants
with the current dependency packages and packaging them together. Full build
output and artifact hashes are retained here. This supersedes the earlier browser
capture for the pre-integration tree.

Headless Chrome 154 ran in a dedicated agent-browser session at 1280 × 800. The
repository's isolated workspace server used localhost port 8873 and arena2d's
Sandbox project, paused in Studio. Root serial `/chibi.html?serial=1` used an
actual `ArrayBuffer` heap. `/threads/chibi.html` used `SharedArrayBuffer`, stayed
on the worker script path and did not fall back. Runtime snapshots record both.

Both variants passed these observed interactions, with screenshots inspected:

- Click Outliner Filter and press the printable keys in `camera`: visible value
  `camera`, one matching Camera row and canvas keyboard focus.
- Drag the pane divider: serial x899 → x799; worker x799 → x899. Scene, Outliner
  and Properties resized together and remained interactive.
- Open Object > Add: the nested menu rendered across hosted document bounds.
  Serial Escape dismissed the submenu first, retained the parent, then dismissed
  the parent. Worker outside press in Properties search dismissed both and focused
  that field.
- Replace Outliner with Assets, then restore Outliner: Assets grid rendered,
  Outliner returned with its `camera` filter and matching Camera row, while Scene
  and Properties remained visible.

Page-error logs are empty. Console logs preserve the informational RmlUi warning
about a long submenu's box-shadow texture exceeding the 800-pixel window region.
This capture does not claim warning-free console output, GPU-device identity or
a performance speedup.

The browser and owned server were stopped, and port 8873 had no listener. No
authored assets or other processes were changed. Git status was clean after root
restored its temporary native comparison source, at the same frozen browser head.
