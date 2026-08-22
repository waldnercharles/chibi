# ADR 0015: layered auto-arrange, measured against the drawn curves

## Status

Accepted. Supersedes the Distribute and Auto arrange decisions in ADR 0014.

## Context

ADR 0014 gave the editor three layout actions. Align averaged the selected
nodes' Y. Distribute packed the selection's boxes along X with a minimum gap
and a sampled crossing score as a veto. Auto arrange assigned ranks from
executable edges only, ran barycenter sweeps, and packed each rank into a
column.

None of the three optimised what makes a node graph readable. Align and
Distribute worked on a selection and a single axis, so they could not know
whether the result was easier or harder to follow; Distribute's crossing veto
could only decline to make things worse. Auto arrange had two structural gaps:
a wire spanning several columns was never given room, so it was drawn straight
through whatever nodes sat between its endpoints, and rank assignment ignored
data edges except through a special case, so a pure data node was stranded in
the leftmost column instead of sitting beside the node it feeds. Both stages
optimised node placement while the thing a reader actually follows is the
cubic curve between two pins, which no stage ever looked at.

## Decision

Align and Distribute are deleted. A selection-and-one-axis nudge is not a
readability tool, and keeping them invited the assumption that they were.

Auto arrange becomes the single layout command, implemented as a Sugiyama-style
layered pipeline in `editor/arrange.c`. That module is pure geometry: it takes
measured boxes, a pin pitch, and a wire list, and returns one position per
node. It knows nothing of ImGui, `graph`, or `editor`, so the layout quality is
asserted headlessly in `test/integration.c` rather than only inspected on
screen. The editor supplies the measurements and writes the results back
through the normal undo snapshot.

The pipeline runs per weakly connected component, and components are stacked
with a gap, so wires from one component cannot tangle with another:

1. Break cycles. Depth-first back edges are laid out reversed. The wire is
   still drawn between its authored pins; only column assignment sees the
   reversal.
2. Rank. Longest path over both edge kinds, then a pass pulling each node right
   onto its earliest consumer. This is what puts a pure data node in the column
   beside the node it feeds, and it replaces ADR 0014's exec-only special case.
3. Reserve corridors. A wire spanning several columns gets one placeholder per
   intermediate column, so it claims vertical space instead of being drawn
   through a node.
4. Order within columns. Median sweeps plus adjacent transposition, keeping the
   best crossing count seen. Counting is port-aware: two wires into different
   rows of the same node are counted in the order they are drawn. A crossing
   between two wires that share an endpoint node counts double, because the
   shared node's pin stack already fixes the order its targets should appear
   in, so such a crossing is an ordering mistake rather than a necessary one.
   It is also drawn beside the shared node where the two curves are still
   near-parallel, which reads as two wires running over each other.
5. Assign coordinates. The priority method, aligning each node's *pin* with its
   neighbour's pin rather than aligning box centres, so a wire between two
   connected pins is drawn flat. Corridors are placed first, which keeps long
   wires straight.
6. Refine. A bounded local search that is the only stage reading the real cubic
   curves: each node is nudged inside the slack its column neighbours leave,
   and a nudge is kept only when the measured cost of the wires and collisions
   it can affect drops.
7. Let nodes leave their column. Columns are a device for readability, not a
   constraint: a node's x does not have to equal its rank's x. A node may move
   into the gutter between two columns, in both axes at once, as long as it
   stays right of its producers and left of its consumers. The bar is a strict
   reduction in crossings and collisions, so the grid stands everywhere it is
   not buying something.

   Both axes have to move together. Pulling a leaf left often lands it on a
   wide node from the previous column, and the vertical room to clear that node
   only exists off-column, so searching one axis and then the other finds
   neither move. This stage carries its own budget: it is an order of magnitude
   dearer per node than stage 6, and sharing one budget let it starve the
   vertical refinement on mid-size graphs.

Stages 1 to 5 optimise a proxy, the crossing count between column orders.
Stage 6 optimises the drawn geometry, weighting a wire crossing a node body
above two wires crossing each other, with wire length only as a tie-break.
`arrange_measure` exposes those counts, so the cost function the layout
minimises is the same one a test asserts against.

Every stage is bounded. Stage 6 additionally carries a work budget, because its
cost grows with the square of the wire count.

## Consequences

Measured on the regression graphs, with node overlaps at zero in every case:

| graph | crossings | wire-node collisions |
| --- | --- | --- |
| 12-node chain with a skip wire | 8 to 0 | 31 to 0 |
| 14-node crosswise fan | 12 to 0 | 30 to 0 |
| 30-node random, cyclic | 255 to 40 | 166 to 4 |
| the `--flow-demo` graph | 1 to 0 | 0 |

Each stage was verified to earn its place by disabling it and re-measuring; the
suite fails when any of ordering, transposition, coordinates, ranking, node
separation, component separation, refinement, the gutter escape, or the
shared-endpoint crossing weight is disabled. The wire corridor stage is the exception: it and refinement
both attack wire-node collisions, and on the regression graphs either one alone
reaches zero, so the suite covers them jointly rather than individually.

The work budgets make the cost bounded rather than small. Past a few hundred
nodes they run out and the earlier stages' result stands, which costs collision
quality rather than time. This is an explicit user action, not a per-frame cost.

The two numbers to keep apart when reading any measurement here are graph size
and wire locality. Wiring random node pairs gives every wire a long column span
and so a corridor per column crossed, which is a stress test rather than a
representative graph. Means over eight seeds at 200 nodes and 300 wires:

| wiring | collisions | crossings | time |
| --- | --- | --- | --- |
| random pairs | 261 | 2462 | 424ms |
| spans of at most 5 columns | 23 | 196 | 109ms |

Against the shipped budgets, locally wired graphs cost 32ms at 50 nodes, 110ms
at 200, and 412ms at 800. The stress case is budget-limited and the local case
is much less so: raising both budgets sixteenfold takes the random-pair case
from 261 collisions to 154 and the local case from 23 to 15, in both cases for
roughly three times the wall clock. The budgets are therefore the quality knob,
and the defaults are set for the local case on the grounds that a real graph is
wired locally.

Single-seed comparisons on these generators are noise. An apparent regression at
200 nodes turned out to reverse over eight seeds; any future tuning should
report means over several seeds.

Some crossings cannot be removed by ordering alone, and that is what stage 7 is
for. The `--flow-demo` graph is the worked example, and the suite locks in that
it comes out with none.

On a strict column grid one crossing there is genuinely forced. A Sequence's
three exec pins fix the order Delay, Branch, Sequence down the next column; the
Bool feeding that Branch must sit in an earlier column, and its wire lands on
the Branch's *second* pin row. A crossing-free arrival order would have to be
Delay.Exec, Branch.Exec, Sequence.Exec, Branch.Condition, which needs the
Branch's second row below the Sequence's first row while the Branch box sits
above the Sequence box: impossible without overlapping the boxes. An exhaustive
search over every forward-only column assignment and a coordinate grid confirms
it, and giving the trailing Sequence a later column raises the count to two.

The conclusion to draw from that is not that the crossing is unavoidable, but
that pinning every node to its rank's x is the thing standing in the way. The
trailing Sequence is a leaf: nothing depends on its x, so it can sit in the
gutter, which vacates the band the Bool's wire needs. Zero crossings, and no
reroute node required.

This case only appears with the real measured box widths. It turns on the Bool
being wide relative to its column, so the regression test uses the editor's
actual metrics; with every box the same width there is no forced crossing to
fix and the test proves nothing.

Keeping a node's fan-out in pin order is a weighted preference, not a
guarantee. On a deliberately crosswise graph in the suite it loses to removing
more crossings elsewhere, leaving two out-of-order wires and six crossings.
The weight is the smallest that breaks the tie; raising it starts trading
unrelated crossings for wire-node collisions without reading better.

What this still does not do: it is a heuristic, not an optimiser, and crossing
minimisation is NP-complete, so a crossing-free result is never promised. It
moves nodes only, so it cannot route a wire around an obstacle the way a
visibility-graph router would; a graph with genuinely unavoidable conflicts
keeps them. The gutter escape widens what node movement can express, but it is
still node movement. It ignores comment boxes. A cyclic graph has no left-to-right
ranking, so its back edges will cross something. Layout is not persisted state:
positions remain the only layout data, and re-running the command on an
unchanged graph is idempotent because every tie-break is deterministic.

Because the seed order comes from the authored Y positions, the result stays
recognisable rather than reshuffling a graph the author has already arranged.

## References

- Sugiyama, Tagawa, Toda, "Methods for visual understanding of hierarchical
  system structures", 1981. The layer/order/coordinate decomposition.
- Gansner, Koutsofios, North, North, "A technique for drawing directed graphs",
  1993. Graphviz dot: rank tightening, median ordering, transposition.
- Brandes and Köpf, "Fast and simple horizontal coordinate assignment", 2002,
  and its erratum. The stronger alternative to the priority method used here.
- Eclipse Layout Kernel, layered algorithm: <https://eclipse.dev/elk/reference/algorithms/org-eclipse-elk-layered.html>
- Hierarchical drawing chapter, Graph Drawing handbook:
  <https://cs.brown.edu/people/rtamassi/gdhandbook/chapters/hierarchical.pdf>
