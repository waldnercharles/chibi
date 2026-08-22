# Editor UX and smoothness audit

- [x] The spawn menu groups descriptors by category and filters by name and
  declared keywords.
- [x] A pin-originated spawn filters incompatible descriptors and offers the
  matching conversion where one exists.
- [x] Zoom-to-fit and auto-arrange are available as toolbar actions and are
  authored through the normal edit snapshot choke point.
- [x] Generic pin groups adopt a connected type and restore their spare pin on
  disconnect.
- [x] Flow rendering consumes traversal records and never calls an evaluator
  callback.
- [x] Layout and node positions are authored data and survive save/load.
- [x] The bounded flow-demo window launches and exits cleanly for a 600-frame
  smoke run at an explicit target rate (`--fps 60`).
- [ ] Human check: drag, pan, zoom, connect, and marquee-select the flow demo at
  30, 60, and 144 FPS without a visible hitch.

The remaining row requires a window session. Headless coverage is in
`test/self_check.c`, and the render-path callback check is run with:

```sh
! grep -nE 'flow_context|node_desc->flow|eval_context' editor/editor.c \
  | grep -E 'ed_draw_link|ed_draw_flow|ed_draw_canvas'
```
