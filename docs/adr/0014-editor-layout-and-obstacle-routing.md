# ADR 0014: constrained editor layout and obstacle routing

## Status

Superseded by ADR 0015 for Distribute and Auto arrange. The Zoom to fit and
link-rendering decisions below still hold.

## Context

The editor's original Distribute action spaced node origins by an array index. It
ignored node dimensions, graph direction, nodes outside the selection, and the
links drawn between nodes. Zoom to fit also treated node origins as points and
centered them at the canvas origin.

A visual scripting graph needs a readable layout rather than a mathematically
even list of points. Layered graph drawing is the appropriate model for a full
auto-arrange operation: assign ranks, order nodes within a rank to reduce
crossings, then assign coordinates with node separation. This is the practical
approach used by Sugiyama-style layout systems such as Graphviz dot and Dagre.
Exact crossing minimization is not guaranteed for arbitrary graphs.

## Decision

The editor uses a bounded, deterministic heuristic that stays inside the C
editor and keeps authored positions as the only layout state:

- Zoom to fit measures complete node AABBs, chooses the largest zoom that fits
  the available canvas with a margin, and centers the resulting world bounds.
- Distribute preserves the selected nodes' existing left-to-right order, using
  vertical position and ID only as tie-breakers. It packs their measured AABBs
  with a minimum horizontal gap, including when the whole graph is selected.
- If a packed node overlaps an unselected node, the remaining packed suffix is
  shifted past that obstacle. A bounded sampled Bézier crossing score accepts the
  full move only when it does not increase crossings; otherwise it tries smaller
  moves and leaves the original layout unchanged when necessary. The operation
  preserves an undo snapshot only when positions change.
- Auto arrange is the separate topology-aware command. It assigns ranks from
  executable edges, places each pure data node before its earliest consumer,
  applies bounded barycenter sweeps within ranks, and places measured AABBs in
  separated columns.
- Link rendering is endpoint-driven. It uses the normal cubic path between the
  two pins and does not move when an unrelated node moves. It does not attempt
  global edge routing; explicit reroute nodes remain the correct future seam
  for intentional wire shaping.

The router is conservative and bounded. Dense or cyclic graphs may still have
unavoidable crossings; it must not trade an impossible crossing for an
unbounded search or a frozen frame.

## Consequences

The common graph case gets non-overlapping node boxes, stable link curves, and
an explicit layered Auto arrange operation without a runtime layout dependency.
Distribute uses the sampled crossing score as a guard against making a layout
more tangled merely because all nodes were selected. The guard is not a global
untangler: existing crossings, near-tangent crossings outside the sample
resolution, and wire-to-node collisions may remain visible instead of being
hidden by surprising automatic detours.
The layout remains reproducible because rank, barycenter, node ID, edge order,
and fixed margins are deterministic tie-breakers. The layout is bounded and cheap to redraw. It does not claim a global
crossing-free result. Dense graphs should use explicit reroutes or a future
stronger layered layout rather than an implicit wire-dodging pass.

The implementation is not a global optimizer. A future large-graph layout
phase could replace it with a full layered layout and visibility-graph router,
but that would require a separate performance and persistence decision.

## References

- Graphviz, `overlap` and separation attributes: <https://graphviz.org/docs/attrs/overlap/>
- Graphviz, spline routing: <https://graphviz.org/Misc/spline-o-matic/>
- Dagre layered layout overview: <https://github-wiki-see.page/m/dagrejs/dagre/wiki>
- Eclipse Layout Kernel layered algorithm overview: <https://eclipse.dev/elk/blog/posts/2025/25-08-21-layered.html>
