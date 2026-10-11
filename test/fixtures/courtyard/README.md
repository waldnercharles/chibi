# Courtyard

A fixed top-down camera over a flagstone courtyard, lit in 2.5D by HRC's
Depth thickness mode. It has no gameplay. `content/images/court.png` holds four
16×16 frames: floor, wall, crate and lantern. `court_normal.png` and
`court_height.png` are bevelled from each frame's edges, 3 texels wide.

- The floor is a tilemap with a normal map, no height and edge light 1. It
  absorbs nothing, so HRC's light reaches it, and Depth thickness shades that
  light by the floor's facing: each flagstone's bevel brightens toward the
  nearest lantern.
- The walls stand 40 units high and absorb light, so HRC stops at them.
- The crates stand 8 units high and absorb nothing. HRC's light passes over
  them, and their height shadows the floor behind them from the lanterns,
  which stand 24 units high.
- 24 small glows lie on the floor, at no height.

Lighting uses bounded segments for visibility, so the crates' shadows read at
this scale, and a 64-unit thickness, so every wall and crate blocks light all
the way down to the floor. Preview it with
`python3 tools/agent.py preview <request> --project-root test/fixtures/courtyard`.
