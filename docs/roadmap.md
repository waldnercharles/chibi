# c-blueprints roadmap

Phased plan from the current state to the parity target in `docs/spec.md`.
Requirement IDs such as `RUN-4` refer to that document. Decisions are recorded
in `docs/adr/`.

`docs/ue5-event-graph-parity-audit.md` audits the catalog against UE 5.8.
Section 0 is reconciled into the spec: the current order after the completed
foundation is an event-driven vertical slice, Functions, Arrays, then
Delegates. ADR 0011 replaces run-scoped behavior state with persistent
Blueprint instances. The pre-audit phases remain at the end of this file only
as historical implementation notes.

## Working agreement

These rules exist because features were previously landed half-implemented: the
data model gained `graph_set_event_name()` and `node.event_name` while the
editor never grew a way to set an event name, and `core/flow.c` grew a complete
execution runtime that no UI ever called.

1. A phase is done when every spec ID in its coverage list is cited by a check
   in `test/self_check.c` or by a manual check in that phase's list, and every
   acceptance criterion passes.
2. `UI-1` is a gate, not an aspiration. A phase that adds an authored value to
   the data model or the file format ships that value's editing surface in the
   same phase. Landing the model half alone is not a partial success, it is a
   regression against `UI-1`.

   The gate covers authored values only, meaning anything a user types, picks,
   toggles, or drags. It does not cover state a run records. Capturing watch
   values under `DBG-5` adds no authored value, so it does not pull the watch
   toggle in `UI-11` forward; toggling a watch is authored and is gated
   normally. The test is whether a user can produce the value, not whether the
   value is serialized.

3. Spec requirements are appended, never renumbered. A requirement that turns
   out wrong is marked withdrawn in place.
4. A phase that discovers a missing requirement adds it to `docs/spec.md` with
   the next free ID in its section, then covers it or defers it explicitly in
   this file.
5. `test/self_check.c` check names begin with the spec ID they cover, so
   `grep` finds uncovered requirements.

## Current state

Verified against the tree at commit `606c694` plus working-tree changes.
`cmake --build build && ./build/self_check` passes.

Present and working:

- `core/graph.c`: node and edge storage, data and exec connection rules, the
  one-driver data rule, the exec order field, the paused-mutation guard.
- `core/eval.c`: pull-based data evaluation with memoization and a cycle guard.
- `core/flow.c`: imperative execution over an explicit `flow_frame`
  continuation stack, latent suspension against a host monotonic clock,
  ordered fan-out, a step budget, and topology-change detection.
- `core/flow_nodes.c`: Event, Branch, Sequence and Sequence4, ForLoop, Delay.
- `core/serialize.c`: graph JSON v2 with data and exec edges, plus subset save
  and append.
- `reflect/reflect.c`: libclang tool emitting prefixed `type_desc`, Make, Break,
  and call descriptors into a build-directory registry pack; target-specific
  pack composition provides the global lookup surface.
- `editor/editor.c`: canvas with placement, dragging, pan, zoom, selection,
  marquee, type-checked linking, deletion, spawn menu with filter, JSON undo
  and redo, clipboard, and typed editors for unconnected scalar inputs.

Known gaps that drive the phase order:

- No execution UI at all. `graph_run_event()`, `flow_run_start()`,
  `flow_run_resume()`, and `flow_run_tick()` are called from zero UI sites.
- No pause, step, stop, or reset in the runtime. `flow_run_resume()` runs to
  completion or to a latent suspension, and nothing else.
- No way to set an event name outside JSON. `graph_set_event_name()` exists and
  `editor/editor.c` never calls it.
- `node_desc` holds file-static pin arrays, so per-instance pins are
  impossible. `sequence_outputs[]` is fixed at four entries.
- Edges reference nodes by array index, and `graph_remove_node()` shifts the
  array, so no stored reference survives a delete.
- `eval_view_refresh()` re-evaluates every data output every frame and writes
  into `n->outputs[pin]`, the same storage execution writes.
- `core/flow.c` and `core/eval.c` keep error state in file-static globals
  (`g_flow_error`, `g_status`).
- No variables, no functions, no macros, no blueprint container, no compile
  pass, no breakpoints, no watches.

## Phase 0: baseline (done)

These requirements are already satisfied by the code described above. They are
listed so the coverage check in `docs/adr/0007` closes, and so a later change
that breaks one is visible as a regression rather than as an unlisted gap.

Estimate: half a day, to add the citing checks.

Covers: `SCOPE-1`, `SCOPE-2`, `TYPE-1`, `TYPE-2`, `TYPE-3`, `TYPE-6`,
`NODE-8`, `BP-9` through `BP-11`, `DATA-1`, `DATA-3`, `DATA-4`, `DATA-5`,
`DATA-6`, `DATA-9`, `EXEC-1`,
`EXEC-2`, `EXEC-3`, `EXEC-4`, `EXEC-5`, `EXEC-6`, `EXEC-9`, `EXEC-10`,
`EXEC-11`, `CAT-2`, `CAT-4`, `CAT-23`, `REFL-1`, `REFL-2`, `REFL-3`, `REFL-4`,
`REFL-6`, `REFL-7`, `UI-39`.

Deliverable: rename the existing `test/self_check.c` checks to lead with the
spec ID they cover, and add checks for the ones with no test today.

Acceptance criteria:

- `SCOPE-1`: no file under `core/` includes a host, ImGui, or ECS header, and
  the `core` CMake target links nothing but yyjson and the generated registry.
- `SCOPE-2`: `self_check` links `core` and `demo` only, and creates no window.
- `TYPE-3`: `grep` finds no `offsetof` arithmetic or byte-level struct access
  outside `pin_write_field` and `pin_read_field`.
- Every ID in the coverage list above is the prefix of at least one check name
  in `test/self_check.c`.

## Phase 1: execution and observability (done)

Goal: a graph can be run, paused, stepped, and stopped from the editor, and a
run produces visible output. Authoring during a run stops it first. No change
to the node or pin model, so this phase is pure addition.

Estimate: 2 to 3 days.

Covers: `SCOPE-3`, `EXEC-12`, `RUN-1` through `RUN-4`, `RUN-6`, `RUN-7`,
`RUN-8`, `RUN-10`, `RUN-11`, `RUN-13`, `RUN-14`,
`CAT-20`, `DBG-5`, `DBG-6`, `DBG-10`, `UI-6`, `UI-7`, `UI-8`, `UI-10`,
`UI-18`, `UI-19`, `UI-36`, `UI-40`, `UI-41`, `DBG-14`.

Deliverables:

1. Remove `g_flow_error` from `core/flow.c` and `g_status` from `core/eval.c`.
   Error and status state moves onto the run and onto an explicit evaluation
   context. Dispatch failures that happen before a run exists return their
   error through an out-parameter. `SCOPE-3`, `DATA-7`.
2. Add external pause and single-step to the run: a stop-request flag checked
   at one point in the step loop, and an activation budget for one step.
   `RUN-3`, `RUN-4`.
3. Add Stop. It ends the run from any non-idle state, releases run storage,
   clears per-node memo, cycle, error, and watch state, and releases the
   mutation guard. `RUN-6`, `RUN-8`, `DBG-14`.
4. Add a host sink to `flow_options`: a callback plus user pointer, reachable
   from a node's execution callback. This replaces the current practice of
   smuggling host data through `flow_inputs.user_data`. `EXEC-12`.
5. Add the Print String node, delivering to the host sink. `CAT-20`.
6. Add watch capture: a run-owned table of recorded pin values, keyed for now
   by node pointer and pin index, with struct values copied into watch storage
   rather than retained as arena pointers. `DBG-5`.
7. Editor toolbar: Compile placeholder, Run, Pause, Resume, Step, and Stop,
   plus an entry-point selector built from the graph's event nodes, and a run
   state and step count readout. Invalid actions are disabled with a hover
   reason. `UI-6`, `UI-7`.
8. Editor output log panel receiving Print String output with its source node,
   selecting and revealing that node on click. Adds `editor_reveal_node()` and a
   Clear action. `UI-8`, `UI-19`, `UI-40`.
9. Suspend `eval_view` while a run is non-idle, and read pin displays from the
   run's recorded values instead. `DBG-6`, `DBG-10`, `UI-10`.
10. Exclude run state transitions from the undo stack. Authoring edits stop
    and release a live run at one editor choke point before mutation. `UI-18`,
    `UI-41`.
11. Host loop in `app/main.c` drives the run with a monotonic time each frame.
    `RUN-10`.
12. Run state is never serialized, and save during a run writes authored data
    only. Load, undo, and every authoring edit stop and release a non-idle run
    before mutation. `RUN-13`, `RUN-14`, `UI-36`, `UI-41`.

Acceptance criteria:

- `SCOPE-3`: `grep -n '^static.*g_' core/*.c` returns no mutable state. Two
  runs started against the same graph report independent errors and step
  counts (`RUN-11`).
- `RUN-4`: stepping N times from an entry activates exactly N nodes, in the
  same order a single `flow_run_resume()` would have taken.
- `RUN-3`: pausing a run that is mid-execution leaves it paused, and resuming
  continues from the next unactivated node.
- `RUN-6`: after Stop, `graph_add_node()` succeeds, every node's memo and
  error flags are clear, watch values are gone, and a fresh run from the same
  entry produces byte-identical output to the first run.
- `RUN-6`: stopping a run suspended inside a Delay leaves the graph mutable and
  activates no further nodes.
- `CAT-20` and `EXEC-12`: a graph of Event to Print String to Print String
  delivers exactly two sink calls in order, each carrying its source node.
- `DBG-5`: a watched struct pin's recorded value is readable after the run is
  freed, and its field values match what executed.
- `DBG-6`: while a run is live, no call into `eval_view_refresh()` occurs, and
  displayed pin values equal the run's recorded values.
- Manual `UI-6`, `UI-7`: every toolbar button is exercised in each run state;
  no button is silently inert.
- Manual `UI-8`, `UI-19`: clicking a log entry selects and centers its node.
- `RUN-13`: saving mid-Delay then loading yields a graph with no run, and the
  saved document contains no run state.
- Manual `UI-18`: running and stepping a graph leaves the undo stack unchanged.
- Manual `RUN-14`, `UI-36`, `UI-41`: undo, load, and authoring edits during a
  run stop it first and then proceed; the UI states that behavior.

## Phase 2: identity, instance pins, and format v3 (done)

Goal: the model can express per-instance configuration, and references survive
editing. This is the foundation every later phase depends on.

Estimate: 3 to 4 days.

Covers: `ID-1` through `ID-6`, `PIN-1` through `PIN-10`, `PIN-14`, `PIN-16`,
`NODE-1` through `NODE-6`, `NODE-11`, `NODE-12`, `EXEC-8`,
`FMT-1` through `FMT-6`, `FMT-8`, `FMT-10` through `FMT-15`, `UI-24`, `UI-32`,
`UI-35`.

Deliverables:

1. ID allocation on the graph, serialized, with retirement. `ID-1` to `ID-4`.
2. Instance pins: `graph_add_node()` copies the descriptor's pin template into
   instance storage. Pin identity, name, type, type name, kind, container,
   authored default, split parent, and flags live on the instance. `PIN-1`,
   `PIN-2`, `PIN-3`.
3. Separate authored defaults from runtime values. `node->inputs` currently
   holds both an authored literal and a computed value, and `node->outputs`
   holds both a const node's authored value and a computed one. Split into an
   authored default on the pin and a runtime value array on the node. `PIN-4`,
   `PIN-5`.
4. Edges reference node and pin IDs. Rewrite `graph_input_source()`,
   `graph_exec_input_source()`, `graph_exec_output_target()`, and
   `graph_remove_edge()` accordingly. `ID-6`.
5. Node title override, comment, reference ID, and reference name fields.
   Delete `node.event_name` and move event names onto the reference name.
   `NODE-3`, `NODE-4`, `NODE-5`.
6. User-extensible pin groups with a declared minimum and maximum, and add and
   remove operations that delete affected edges. Sequence becomes extensible
   from two with no maximum, replacing Sequence4. `PIN-14`, `CAT-3`.
7. Serialization v3. Delete the versionless, v1, and v2 loaders. Rewrite subset
   save and append against IDs. `FMT-1` to `FMT-6`, `FMT-8`, `FMT-10`,
   `FMT-11`.
8. Deterministic output ordering and formatting. `FMT-13`.
9. Update `editor/editor.c` layout and hit testing to read instance pins rather
   than `desc->input_count` and `desc->outputs[p]`.
10. Descriptors declare pure or impure explicitly rather than leaving it to be
    inferred from pin kinds. `NODE-6`.
11. Extend the mutation guard to every non-idle run state and to pin name, type,
    and count changes, which become mutable in this phase. `EXEC-8`.
    Re-entrant activation is implemented here, but its requirement `EXEC-14`
    is verified in Phase 6: half of it concerns run-scoped per-node scratch,
    which is `EXEC-13` and does not exist yet, and a check covering only the
    reachable half while claiming the ID would convert a real gap into false
    confidence.
12. Node disable, with pass-through execution and typed zero outputs, plus its
    context menu entry and distinct rendering. `NODE-11`, `UI-32`.
13. Display classes on descriptors and a single color table for node and pin
    coloring. `NODE-12`, `PIN-16`, `UI-35`.
14. Add and remove pin controls on the node for user-extensible groups.
    `PIN-14`, `UI-24`.
15. Blueprint metadata narrowed to the four fields in `FMT-14`, and clipboard
    moved to the host text clipboard. `FMT-14`, `FMT-15`.
16. Exec inputs become multi-driver. Remove the current replacement behavior so
    multiple incoming exec edges merge at one node, as required by `PIN-8`.
17. Delete the versionless and v1 data-only loaders, alongside the v2 loader,
    so `FMT-9` is enforced rather than retained as legacy compatibility.

Acceptance criteria:

- `ID-4`: deleting node A then adding node B gives B a different ID from A, and
  an edge that referenced A resolves to nothing rather than to B.
- `ID-5`: copying two connected nodes and pasting twice yields four nodes with
  four distinct IDs and two correctly rewired edges.
- `PIN-3`: renaming and retyping an instance pin on one node leaves every other
  node of the same descriptor unchanged.
- `PIN-4`: evaluating a graph does not modify any authored default; saving
  before and after a run produces identical documents.
- `PIN-14`: a Sequence extended to seven outputs runs all seven in order, and
  removing output three deletes its edge and leaves the rest wired.
- `FMT-12`: load then save then load then save is byte-identical for a graph
  exercising every pin type, both edge kinds, extended pin groups, and every
  node field.
- `FMT-9`: a v2 document is rejected with a message naming the version.
- `EXEC-8`: renaming a pin, retyping a pin, and adding a pin are all rejected
  while a run is suspended inside a Delay, and also while it is merely paused.
- `PIN-8`: two exec edges into one node both fire it, because exec paths merge
  at inputs rather than replacing one another.
- `NODE-11`: disabling a node in the middle of a three-node exec chain runs the
  first and third, and re-enabling it restores all three with edges intact.
- `FMT-15`: copying two nodes, then pasting into a second editor process,
  reproduces both nodes and their edge.
- Manual: every existing editor operation still works, and node deletion no
  longer disturbs unrelated edges.

## Phase 2b: Real compatibility and connection feedback (done)

Goal: close the two current editor gaps that converge on one common action.
Connecting a `const_f32` to a `Delay`'s Duration was refused because Duration
is `PIN_F64`, and the editor gave no reason. The inaccurate literal-completeness
claim in `CAT-28` is withdrawn; corrected literals ship in Phase 3 under
`CAT-30`.

Small enough to run before the Phase 8 polish work.

Estimate: 1 day.

Covers: `TYPE-10`, `UI-22`, `UI-52`.

Deliverables:

1. `PIN_F32` and `PIN_F64` become one Real category for connection purposes,
   converting at the boundary, with one shared Real colour. Integer widths stay
   distinct. `TYPE-10`.
2. A rejected connection says why at the cursor, naming both pin types.
   `UI-22`. Moved here from Phase 3, because until it exists every type
   mismatch presents as a silent failure.
3. Canvas shortcuts stop being suppressed by any active widget. `UI-52`.
   `ed_handle_shortcuts()` returned early on `ImGui_IsAnyItemActive()`, so a
   focused inline pin field killed copy, paste, cut, duplicate, undo and
   delete at once. It surfaced as a bug report about copy and paste failing on
   ForLoop, that being the node whose value fields were being edited.

Acceptance criteria:

- `TYPE-10`: a `const_f32` output connects to a `Delay` Duration input and the
  run observes the converted value; a Real pin of either width renders in the
  same colour.
- `TYPE-6`: an Integer output still refuses an Integer64 input, so unifying Real
  did not loosen integer widths.
- Manual `UI-22`: dragging a String output onto an Integer input states both
  type names and does not connect.
- Manual `UI-52`: a focused text-entry field has a visible yellow outline and
  owns Ctrl+C and Ctrl+V. Escape or a canvas click releases it; the same
  shortcuts then copy and paste the selected node. Activating a non-text widget
  does not suppress canvas shortcuts.

## Phase 3: event-driven vertical slice (done)

Goal: a consumer creates a persistent Blueprint instance, dispatches authored
events, and observes stateful behavior through reflected host calls. This is
one end-to-end slice, not a catalog batch.

Estimate: split before implementation. Runtime-instance scheduling and
reflection v2 require separate commits, each with headless acceptance checks.

Covers: `SCOPE-4`, `SCOPE-5`, `TYPE-11` through `TYPE-14`, `PIN-17`, `NODE-13`,
`VAR-1`, `VAR-3` through `VAR-7`, `VAR-10`, `VAR-11`, `BP-1`, `BP-8`, `BP-12`,
`BP-13`, `EVT-1` through `EVT-7`, `EVT-9` through `EVT-11`, `EXEC-15` through
`EXEC-19`, `RUN-1` through `RUN-4`, `RUN-10`, `RUN-12`, `RUN-13`, `RUN-15`
through `RUN-20`, `CAT-2`, `CAT-3`, `CAT-18`, `CAT-20`, `CAT-23`, `CAT-27`,
`CAT-29`, `CAT-30`, `CAT-32`, `CAT-33`, `CMPL-1` through `CMPL-8`, `CMPL-12`,
`CMPL-16` through `CMPL-20`, `FMT-2`, `FMT-16` through `FMT-18`, `UI-1`
through `UI-9`, `UI-17` through `UI-19`, `UI-22` through `UI-24`, `UI-29`,
`UI-30`, `UI-42`, `UI-43`, `UI-53` through `UI-56`, `REFL-9` through
`REFL-14`, `DBG-17`.

Runtime and language deliverables:

1. Introduce the minimal Blueprint definition and `blueprint_instance` needed
   for one default Event Graph, authored defaults, optional owner type, and
   independent runtime state. `BP-1`, `BP-8`, `BP-12`, `BP-13`, `VAR-11`,
   `RUN-15`.
2. Replace one-run behavior lifetime with an instance scheduler and separate
   event activations. Instance variables and node scratch persist; stacks,
   temporaries, memo, errors, watches, and latent state stay activation-local.
   `EXEC-15` through `EXEC-19`, `RUN-16` through `RUN-20`.
3. Make Event and Call Event fully authorable with generic typed signatures.
   Core assigns no lifecycle names. `EVT-1` through `EVT-7`, `EVT-9`,
   `EVT-11`.
4. Add `PIN_NAME`, nominal `PIN_HANDLE`, optional typed Self, and corrected
   Bool, Byte, Integer, Integer64, Real, Name, and String literal nodes.
   `TYPE-11` through `TYPE-13`, `CAT-30`, `CAT-32`.

Reflection and editor deliverables:

1. Land reflection v2: explicit `NODE_FN` and `NODE_EXEC`, explicit output
   parameters, generated service types and binding helpers, stable service
   pins, borrowed activation snapshots, and structured missing-service errors.
   `PIN-17`, `REFL-9` through `REFL-14`.
2. Generate the operator signature table and resolve Add, Subtract, Multiply,
   Divide, equality, ordering, And, Or, and Not through exact signatures and
   the UE-facing widening ladder. `TYPE-14`, `CAT-33`, `CMPL-19`.
3. Finish details, tree, compiler, output-log, event-signature, owner-type, and
   preview-instance surfaces required by `UI-1`. Runtime state remains absent
   from JSON and undo, and the edit choke point tears down the preview instance
   before mutation. `FMT-16` through `FMT-18`, `UI-1` through `UI-9`, `UI-17`,
   `UI-18`, `UI-53` through `UI-56`.
4. Use demo conventions `Begin()`, `Tick(Real DeltaSeconds)`, and
   `Input(Name Action, Real Value, Bool Pressed)` without registering mandatory
   core lifecycle entries. `EVT-10`.

Runtime acceptance:

- Begin initializes one instance's variables once. Later Tick and Input events
  read and write the same state. `VAR-11`, `EVT-10`.
- A Delay can suspend while later Tick or Input activations execute. The
  scheduler's activation order is deterministic across repeated runs.
  `EXEC-17`, `CAT-18`.
- Two instances of one definition share no owner, variable, scratch, service,
  activation, error, step-count, or watch state. `RUN-18`.
- Stopping one activation preserves instance variables and scratch. Destroying
  the instance cancels every activation and releases both. `RUN-15`, `RUN-17`.
- Pure reflected calls never execute as side effects during preview. An
  executable reflected call never runs without an exec activation. `NODE-13`,
  `REFL-9`.

Generated and editor acceptance:

- A connected service pin overrides injection. A bound unconnected pin may
  hide. An unresolved pin reappears and execution reports its pin and service
  type. `PIN-17`, `REFL-12`, `UI-53`.
- Each literal has one Value input and one return output and round-trips its
  authored value. Name spelling and the optional owner type round-trip exactly.
  `CAT-30`, `FMT-16`, `FMT-17`.
- A consumer adds a reflected operator signature without changing core. Exact
  matches beat widening; ambiguous or absent candidates produce one blocking
  diagnostic. `CAT-33`, `CMPL-19`, `REFL-13`.
- Saving before and after execution is byte-identical for authored data. An
  edit destroys the preview instance at the one choke point, releases its last
  mutation guard, clears runtime state, and then succeeds. `FMT-16`, `EXEC-19`,
  `RUN-19`, `RUN-20`, `UI-55`, `UI-56`.
- Manual: the entire Begin/Tick/Input demo is authored in the editor without
  JSON edits, and the preview distinguishes its instance from the selected
  activation. `UI-1`, `UI-54`.

## Phase 3b: explicit conversions and Format Text (done)

Goal: numeric widening and scalar-to-string are expressible, and formatted
output unblocks the demo's Begin/Tick/Input verification, which cannot print a
numeric value without a conversion. Pulled forward from Phase 7 slice 1 because
the Phase 3 demo needs it now.

Estimate: 1 to 2 days.

Covers: `TYPE-7`, `CAT-26`, `CAT-27`, `CAT-38`, `UI-22`.

Deliverables:

1. Conversion nodes for every `TYPE-7` conversion: integer widening within a
   signedness (Byte to Integer to Integer64, and the unsigned ladder), integer
   to Real, and any scalar to String. Each is pure with one typed input and one
   typed output. `CAT-26`, `TYPE-7`.
2. Connection-time insertion: when a connection is rejected but `TYPE-7`
   permits it, the editor offers to insert the matching conversion node in the
   same place it states the reason, and wires it on accept. A lossy conversion
   (Real to Integer) is NOT offered. `UI-22`.
3. Format Text: pure, String format input, user-extensible wildcard argument
   inputs (adopt the connected type, revert on disconnect), one String output.
   Substitutes `{0}`, `{1}`, ... and bare `{}` with each argument's value; `{{`
   and `}}` render literal braces; a missing argument renders empty. `CAT-38`.
   This lands `PIN-11`'s single-pin adopt/revert for the first time; full
   wildcard-group resolution stays in Phase 5.
4. Category paths and search keywords for the new nodes. `CAT-27`.

Acceptance criteria:

- `TYPE-7`: connecting an Integer output to a Real input inserts a visible
  conversion node and the run observes the converted value; a Real output to an
  Integer input is rejected with no conversion offer.
- `CAT-26`: every `TYPE-7` conversion converts correctly headless (integer
  widening, integer to Real, Bool/Integer/Real/Name to String).
- `CAT-38`: Format Text with `"Hello {0} World"` and a String argument renders
  `"Hello Bob World"`; `"n={}"` with an Integer 42 renders `"n=42"`; `{{` renders
  a literal brace; `{1}` with one argument renders empty; a Real argument
  renders identically to its pin display.
- `PIN-11`: connecting an Integer to a wildcard argument pin adopts Integer,
  and dropping the edge reverts the pin to unresolved (Generic).
- `UI-22`: dragging a Real output onto an Integer input states both type names
  and does not offer a conversion; dragging an Integer onto a Real offers the
  insertion.
- `python3 tools/spec_coverage.py` stays OK; new checks cite `CAT-26`, `CAT-38`,
  `TYPE-7`, `UI-22`.

## Phase 4: functions and multiple graphs (done)

Goal: behavior can be decomposed without growing one Event Graph indefinitely.
Macros do not enter this phase.

Estimate: 4 to 6 days after Phase 3 is stable.

Covers: `VAR-13`, `BP-2` through `BP-6`, `FN-1` through `FN-6`, `NODE-7`,
`EXEC-7`, `RUN-5`, `RUN-9`, `CMPL-7`, `CMPL-9`, `CMPL-10`, `UI-3`, `UI-4`,
`UI-21`, `UI-30`, `UI-31`.

Deliverables:

1. Add function graphs, signature authoring, Return, and pure and executable
   call nodes. `BP-2` through `BP-6`, `FN-1` through `FN-5`.
2. Add per-call local variables, recursion limits, and structured call stacks.
   `VAR-13`, `FN-4`, `FN-6`, `EXEC-7`, `RUN-9`.
3. Add graph tabs, call navigation, find references, and Step Over. `RUN-5`,
   `UI-21`, `UI-30`, `UI-31`.

Acceptance criteria:

- An executable function returns before its caller continues; a pure function
  participates in pull evaluation and computes once per activation.
- Two recursive calls keep distinct locals and fail at the configured depth
  limit without using the C call stack.
- A Delay in a function graph produces one blocking diagnostic.
- Step Over pauses at the caller's next node and exposes a correct call stack.

## Phase 5: Arrays and loops (done)

Goal: ship the most-used collection workflows before Set and Map key traits.

Estimate: split by storage, wildcard typing, operations, then loops. Before
code, fix per-type copy, destruction, and equality callbacks in a narrow ADR;
Arrays do not require Set or Map hashing.

Covers: `TYPE-5`, `TYPE-8`, `TYPE-9`, `PIN-11`, `VAR-12`, `CAT-5` through
`CAT-7`, `CAT-34`, `DATA-8`, `CMPL-12` through `CMPL-14`, `UI-13`, `UI-14`.

Deliverables:

1. Add typed Array values with explicit copy, move, default, and destruction
   rules. `TYPE-8`, `TYPE-9`.
2. Add wildcard resolution and the complete deterministic operation set in
   `VAR-12`. `PIN-11`, `CAT-34`, `CMPL-12`.
3. Add ForEach, ForEach with Break, ForLoop with Break, and WhileLoop under the
   existing step budget. `CAT-5` through `CAT-7`.
4. Defer Shuffle, random-item, and typed Sort until their random-service and
   ordering contracts are accepted.

Acceptance criteria:

- Every deterministic `VAR-12` operation is checked for empty, single-element,
  multi-element, invalid-index, copy, and destruction behavior.
- Disconnecting the last edge in a wildcard group returns the entire group to
  unresolved; a connected unresolved group blocks compile.
- ForEach over five structs delivers five copied values and indices zero to
  four. The break variant stops at index two and runs Completed once.
- The spawn menu filters array operations by the dragged element type.

## Phase 6: delegates and event dispatchers (done)

Goal: typed communication between persistent Blueprint instances.

Estimate: not set until the decision gate closes.

Covers: `EVT-12` through `EVT-18`.

Decision gate before code:

1. Write an ADR fixing callback identity, weak target lifetime, duplicate and
   removal comparison, invocation ordering, reentrancy, signature evolution,
   serialization, and destroyed-target behavior.
2. Replace the broad `EVT-12` marker with append-only executable requirements
   and update this phase's acceptance checks.

No delegate implementation starts before that ADR.

## Phase 7: common language completion (done)

Goal: close the remaining high-utility fixed language families as separate,
shippable slices. The order inside this phase remains open until each slice's
decision gate is reached.

Covers: `CAT-36`, `TYPE-4`, `TYPE-7`, `PIN-12`, `PIN-13`, `PIN-15`, `BP-7`,
`FN-7`, `FN-9` through `FN-12`, `CAT-9` through `CAT-16`, `CAT-19`, `CAT-22`,
`CAT-26`, `CAT-31`, `CAT-35` through `CAT-37`, `CMPL-11`, `CMPL-15`, `NODE-9`, `NODE-10`,
`REFL-5`, `REFL-8`, `UI-25` through `UI-28`, `UI-31`, `UI-33`, `UI-37`,
`UI-38`.

Note: `TYPE-7`, `CAT-26`, and `CAT-38` (explicit conversions and Format Text)
moved to Phase 3b. They remain in this coverage list only for traceability.

Slices:

1. Switch on Int, String, Name, and Enum; Select; enum reflection.
2. DoOnce, DoN, FlipFlop, Gate, MultiGate, and Retriggerable Delay using
   instance scratch.
3. Struct Make, Break, Set Members, split and recombine, and enum literals.
4. Sets and Maps after equality, hashing, key eligibility, mutation, and
   deterministic-save rules are accepted.
5. Macros, macro libraries, wildcard-per-call resolution, collapse, and expand
   after expansion, local-state, and debugger-source mapping are accepted.

## Phase 8: debugger and editor completion (done)

Goal: adapt debugging and editor polish to persistent instances and overlapping
activations without expanding the language surface.

Estimate: split after the Phase 3 preview debugger exposes real usage.

Covers: `DBG-1` through `DBG-16`, `NODE-11`, `NODE-12`, `UI-10` through
`UI-12`, `UI-15`, `UI-16`, `UI-20`, `UI-32`, `UI-34`, `UI-35`, `UI-37`,
`UI-38`, `UI-44` through `UI-52`.

Deliverables retain the breakpoint, watch, traversal, heat, marker, minimap,
shortcut, layout, and animation requirements already defined in the spec.
Before implementation, fix debugger pause scope and selected-activation behavior
for overlapping activations.

## Phase 9: advanced parity gates (reconciled)

Goal: keep advanced UE families visible without deciding their semantics early.

The UE 5.8 parity audit and ADR 0007 close the classification gates without
adding executable requirements. Async actions, timelines, consumer casts,
localized Text, and engine services remain explicitly deferred or Consumer /
Generated. No advanced implementation is claimed.

- Async actions and timelines need cancellation, owner destruction, service
  lifetime, completion ordering, persistence, and debugger decisions.
- Consumer casts and Is Valid need nominal hierarchy and validity contracts.
- Localized Text needs localization history and serialization contracts.
- Engine input, networking, animation, AI, UI, asset, and ECS nodes remain
  Consumer or Generated families rather than core catalog entries.

## Archived pre-audit plan

The following sections preserve estimates, rationale, and acceptance details
from the earlier catalog-first order. They are not the current implementation
sequence and MUST NOT be used to start work. Requirements cited here remain
traceable until their replacement phase gains equivalent detailed checks.

### Former phase 3: details panel, events with parameters, diagnostics

Goal: the editor becomes the only place authoring happens. Custom events get
real signatures. Diagnostics get infrastructure that later phases extend.

Estimate: 4 to 5 days.

Covers: `EVT-1` through `EVT-8`, `RUN-12`, `DBG-9`, `CMPL-1` through `CMPL-6`,
`CMPL-8`,
`CMPL-16` through `CMPL-18`, `DBG-1` through `DBG-4`, `DBG-7`, `DBG-8`,
`DBG-11` through `DBG-13`, `FMT-7`, `UI-2`, `UI-42`, `UI-43`, `UI-4`, `UI-5`,
`UI-9`, `UI-11`,
`UI-12`, `UI-16`, `UI-17`, `UI-23`, `UI-29`, `UI-30`, `UI-34`.

Deliverables:

1. Details panel driven by selection, exposing every authorable node property
   and every per-pin property. `UI-4`, `UI-5`.
2. Panel layout, dockable from the start. Set `ImGuiConfigFlags_DockingEnable`,
   host a dockspace with `ImGui_DockSpaceOverViewport()`, and make the canvas,
   output log, details panel and compiler results independent windows that can
   be undocked, torn off, resized and rearranged, with layout persisting through
   the already-ignored `imgui.ini`. Docking moved here from Phase 8 because this
   phase creates those panels anyway, and building them behind fixed splitters
   first would mean laying out the same windows twice. Verified available in this
   build: `ImGuiConfigFlags_DockingEnable`, `ImGui_DockSpace` and
   `ImGui_DockSpaceOverViewport` are exposed by cute_framework's bundled
   dcimgui.h. `UI-2`, `UI-42`, `UI-43`.
3. Event name editing in the details panel, replacing the JSON-only path.
   `EVT-2`.
4. Custom event parameter list editing: add, remove, rename, retype, reorder,
   and default per parameter, on top of phase 2 instance pins. `EVT-4`.
5. Call Event node referencing an event by ID, with pins mirroring the event's
   parameters and revalidation on signature change. `EVT-5`, `EVT-6`.
6. Lifecycle events Begin and Tick, host-triggered. `EVT-8`.
7. Diagnostic list type, compile entry point, ordering, and navigation, with
   the rules expressible now: a missing required exec input, an incompatible
   data edge, a data cycle, and duplicate names. The cycle case already exists
   as an evaluation error and becomes a diagnostic here. `CMPL-1` to `CMPL-6`,
   `CMPL-8`, `CMPL-16`, `CMPL-17`.
8. Compiler results panel. `UI-9`. With a compile pass finally existing, a run
   refuses to start against compile errors, and a structured run error becomes
   navigable through the same diagnostic path. `RUN-12`, `DBG-9`.
9. Breakpoints and watches keyed by ID, persisted, with pause-before-activation,
   individual and global enable, canvas rendering, and inline watch values.
   `DBG-1` to `DBG-4`, `FMT-7`, `UI-11`, `UI-12`.
10. Exec traversal recording and call stack presentation. `DBG-7`, `DBG-8`.
11. Keymap as a data table, listable in the UI. `UI-16`.
12. Undo coverage for pin edits, parameter list edits, and details panel edits.
    `UI-17`.
13. Watches on pure pins captured during data evaluation, watches and
    breakpoints discarded with their node or pin, and per-node failure rendering
    with pin diagnostic tooltips. `DBG-11` to `DBG-13`.
14. A required-input badge on nodes. `UI-23`. Connection rejection feedback
    landed earlier, in Phase 2b.
15. Find in blueprint and find references, both navigable. `UI-29`, `UI-30`.
16. Breakpoint list with per-breakpoint and global toggles. `UI-34`.
17. Disabled nodes excluded from diagnostics except one warning. `CMPL-18`.

Acceptance criteria:

- `EVT-2` and `UI-1`: a graph with two named events, both named through the
  details panel, dispatches each by name correctly, with no JSON editing at any
  point.
- `EVT-4`: a custom event with three parameters of differing types delivers all
  three to the first activated node, and reordering them reorders the delivered
  values.
- `EVT-5`: retyping parameter two invalidates only the edges on parameter two
  and reports one diagnostic per invalidated edge.
- `EVT-3` and `CMPL-8`: two events sharing a name produce one diagnostic, and
  dispatch by that name fails rather than picking one.
- `DBG-2`: a breakpoint on the third node of a five-node chain pauses with
  exactly two nodes activated, and resuming activates the remaining three.
- `DBG-1` and `FMT-7`: breakpoints and watches survive save, load, undo, and
  redo, still attached to the same nodes and pins.
- `CMPL-17`: every diagnostic in the panel selects and reveals its node.
- Manual `UI-5`: for every node type in the catalog, every property the file
  format records is editable in the details panel. This check is a per-node
  walkthrough, not a spot check.
- `DBG-11`: a watch on a pure Make node's output reports a value after a run,
  even though that node is never activated by the imperative evaluator.
- `DBG-12`: deleting a watched and breakpointed node, then adding a new node,
  leaves the new node with neither.
- Manual `UI-17`: undo reverses a pin rename, a parameter reorder, and a
  parameter delete.
- Manual `UI-30`: find references on a variable used by three nodes lists all
  three and navigates to each.
- Manual `UI-42`, `UI-43`: panels can be undocked, moved, resized and redocked,
  and the layout survives a restart.

### Former phase 4: variables

Goal: graphs hold state.

Estimate: 3 to 4 days.

Covers: `VAR-1` through `VAR-8`, `VAR-10`, `CMPL-7`, `CMPL-15`, `UI-3`.

Deliverables:

1. Variable list on the graph with ID, name, type, type name, container,
   authored default, and category. `VAR-1`.
2. Get and Set descriptors that derive their pins from the referenced variable
   by ID. `VAR-4`, `VAR-5`.
3. Run-owned variable storage initialized from defaults, independent per run.
   `VAR-8`.
4. Retype and delete handling with edge invalidation and diagnostics. `VAR-6`,
   `VAR-7`, `CMPL-7`.
5. Blueprint tree panel listing variables and events, with create, rename,
   delete, and select. `UI-3`.
6. Details panel rows for a selected variable. `UI-4`.
7. Promote a pin to a variable. `VAR-10`.
8. Unread and unwritten variable warnings. `CMPL-15`.

Acceptance criteria:

- `VAR-4`: a graph that sets a variable in one branch and reads it in a later
  node observes the written value.
- `VAR-8`: two concurrent runs writing the same variable to different values
  each read back their own value, and neither observes the other.
- `VAR-5`: renaming a variable updates every Get and Set node's display without
  touching any edge.
- `VAR-6`: retyping a variable from int to string invalidates exactly the edges
  that became incompatible and reports one diagnostic each.
- `VAR-2`: a local variable declared in a function graph is absent from the
  event graph's variable pickers.
- Manual `VAR-10`: promoting an unconnected float input creates a float
  variable, wires a Get node, and the variable appears in the tree.

### Former phase 5: blueprints, functions, and macros

Goal: graphs become reusable. This is the largest phase because it introduces
the blueprint container and two new graph kinds.

Estimate: 5 to 7 days.

Covers: `BP-1` through `BP-8`, `FN-1` through `FN-11`, `NODE-7`, `RUN-9`,
`EXEC-7`,
`CMPL-9` through `CMPL-11`, `RUN-5`, `UI-21`.

Deliverables:

1. Blueprint container owning the ID counter, variables, and graphs. The ID
   counter moves off the graph. `BP-1` to `BP-4`, `BP-8`.
2. Graph kinds with their entry and return rules. `BP-2`, `BP-5` to `BP-7`.
3. Function graphs: signature authoring on entry and return nodes, call nodes
   by graph ID, synchronous call semantics, local variables, pure functions,
   and a call depth limit. `FN-1` to `FN-6`, `EXEC-7`.
4. Macro graphs: compile-time expansion, multiple exec inputs and outputs,
   latent nodes permitted, per-call-site wildcard resolution, recursion
   rejection. `FN-7` to `FN-10`.
5. Collapse a selection into a function or macro, and expand a call. `FN-11`.
6. Descriptors declare whether they are latent, which is what `CMPL-9` tests.
   `NODE-7`.
7. Compile rules for the new kinds. `CMPL-9` to `CMPL-11`.
8. Step over a call. `RUN-5`.
9. The run exposes its call stack as an ordered list of graph and node IDs,
   which only becomes meaningful once calls exist. `RUN-9`.
10. Several open graphs with per-graph canvas state. `UI-21`.
11. File format extension to a blueprint document with multiple graphs.

Acceptance criteria:

- `FN-3`: a function called from an event completes fully before the caller's
  next node activates, verified by activation order.
- `FN-5`: a pure function's call node participates in data evaluation, computes
  once per activation under `DATA-2`, and needs no exec wire.
- `FN-6`: a function recursing past the depth limit produces a structured
  error, and the process does not crash.
- `CMPL-9`: placing a Delay in a function graph produces exactly one
  diagnostic, and `RUN-12` refuses to start.
- `RUN-9`: a run paused three calls deep reports a call stack of three frames,
  outermost first, and each frame reveals its node.
- `FN-7`: a macro used at two call sites with different wildcard types expands
  correctly at both, and the expansion is invisible to the run's step count
  ordering relative to an inlined equivalent.
- `FN-9` and `CMPL-11`: a macro calling itself produces one diagnostic and does
  not hang the compile.
- `RUN-5`: stepping over a call node advances past it in one step regardless of
  how many nodes the callee activates.
- Manual `FN-11`: collapsing three connected nodes with two boundary data edges
  and one boundary exec edge produces a function with the matching signature,
  and expanding it restores the original graph.

### Former phase 6: catalog completion

Goal: the flow-control and container catalog matches Blueprints.

Estimate: 5 to 7 days.

Covers: `EXEC-13`, `EXEC-14`, `PIN-11`, `TYPE-5`, `TYPE-8`, `TYPE-9`, `DATA-8`,
`VAR-9`, `CAT-1`, `CAT-5` through `CAT-19`, `CAT-24`, `CAT-25`, `CAT-27`,
`CMPL-12`, `CMPL-13`, `CMPL-14`, `UI-13`, `UI-14`, `UI-28`.

Deliverables:

1. Run-scoped per-node scratch storage keyed by node ID. `EXEC-13`. With that
   in place, re-entrant activation becomes testable: a loop body re-entering a
   node already on the continuation stack completes correctly, its scratch is
   shared across the nested activations, and its data memo is not. `EXEC-14`.
2. Wildcard pins with groups, resolution on connect, and unresolve on
   disconnect. `PIN-11`, `CMPL-12`.
3. Containers: array, set, and map pin types and values, with the operation
   node set. Connection compatibility gains its container term, which completes
   `TYPE-5`. `TYPE-5`, `TYPE-8`, `TYPE-9`, `VAR-9`, `CAT-24`.
4. Data evaluation errors for the cases this phase introduces: an unresolved
   wildcard and a retired reference. `DATA-8`.
5. Loop nodes: ForLoopWithBreak, WhileLoop, ForEachLoop and its break variant.
   `CAT-5` to `CAT-7`.
6. Stateful nodes: DoOnce, DoN, FlipFlop, Gate, MultiGate. `CAT-8` to `CAT-12`.
7. Switch on Int, String, and Enum, with per-node case authoring in the details
   panel and a derived read-only list for the enum form. `CAT-13` to `CAT-15`,
   `UI-28`.
8. Select and IsValid. `CAT-16`, `CAT-17`.
9. Delay retrigger semantics and RetriggerableDelay. `CAT-18`, `CAT-19`.
10. Arithmetic, comparison, boolean, and string node sets. `CAT-25`.
11. Category paths and search keywords on every descriptor, with a grouped,
    context-sensitive spawn menu and wire-drop creation. `CAT-27`, `UI-13`,
    `UI-14`.
12. Reachability and unconnected-input warnings. `CMPL-13`, `CMPL-14`.
13. Audit every catalog entry against its details panel rows, closing the
    `CAT-1` and `UI-1` gate for the completed catalog. `CAT-1`.

Acceptance criteria:

- `EXEC-13`: a DoOnce fires once across many activations in one run, and fires
  again in a fresh run without a manual reset.
- `EXEC-14`: a ForLoop whose body re-enters a node already on the continuation
  stack completes every iteration, that node observes one shared scratch across
  the nested activations, and each activation re-evaluates its data inputs
  rather than reusing the outer activation's memo.
- `CAT-11`: a Gate opened, closed, and toggled in a scripted order passes
  exactly the expected activations.
- `CAT-12`: a MultiGate with five outputs and loop enabled cycles through all
  five and wraps; with loop disabled it stops after five.
- `CAT-7`: a ForEachLoop over a five-element array of structs delivers five
  elements with indices zero to four, and its break variant stops at index two.
- `CAT-15`: a Switch on Enum derives one output per enum entry, and adding an
  entry to the enum adds an output without losing existing edges.
- `PIN-11`: connecting a float to a Select option resolves every option and the
  output to float, and disconnecting the last edge returns them to unresolved.
- `CAT-19`: a RetriggerableDelay activated twice inside its window completes
  once, at the later deadline; `CAT-18` Delay completes once, at the earlier.
- `UI-13`: dragging from a bool output opens a menu containing Branch and not
  containing Make Vec3.

### Former phase 7: type system and editor polish

Goal: close the remaining authoring and presentation gaps.

Estimate: 4 to 6 days.

Covers: `TYPE-4`, `TYPE-7`, `PIN-12`, `PIN-13`, `PIN-15`, `NODE-9`, `NODE-10`,
`CAT-21`, `CAT-22`, `CAT-26`, `REFL-5`, `REFL-8`, `UI-15`, `UI-20`, `UI-25`,
`UI-26`, `UI-27`, `UI-31`, `UI-33`, `UI-37`, `UI-38`.

Deliverables:

1. Labelled enum descriptors from reflection, and enum dropdowns instead of raw
   integers. `TYPE-4`, `REFL-5`, `UI-27`.
2. Explicit conversion nodes and editor-inserted conversion on incompatible
   connect. `TYPE-7`, `CAT-26`.
3. Struct pin split and recombine, from the pin's context menu. `PIN-12`,
   `UI-25`.
4. Advanced pin visibility with a per-node expander. `PIN-13`, `UI-26`.
5. Reference pins. `PIN-15`.
6. Comment boxes with contained-node dragging, and editable title, color, and
   size. `NODE-9`, `UI-33`.
7. Reroute nodes for data and exec, insertable by double-clicking a wire, and
   double-click on a call node opens its graph. `NODE-10`, `UI-31`.
8. Struct and enum literal nodes, and struct-valued details panel editors.
   `CAT-21`, `CAT-22`.
9. Reflection tool fails the build on an unsupported annotated declaration.
   `REFL-8`.
10. Canvas polish: hover highlighting of nodes, pins, and links, live type
    feedback while linking. `UI-15`.
11. Minimap. `UI-20`.
12. Auto arrange, straighten connection, and zoom to fit. `UI-37`.
13. Keyboard node spawning by held key plus canvas click. `UI-38`.

Acceptance criteria:

- `TYPE-7`: connecting an int output to a float input inserts a visible
  conversion node, and no silent coercion path exists anywhere in `core`.
- `PIN-12`: splitting a Vec3 input yields three float pins whose values feed
  the same struct, and recombining restores the original pin and its default.
- `TYPE-4`: an enum pin's details row lists labels, and saving records the
  value rather than the label index position.
- `NODE-9`: dragging a comment box moves every node fully inside it and no node
  partly outside it.
- `NODE-10`: a reroute node between two pins is transparent to evaluation and
  to the run's step count.
- `REFL-8`: annotating an unsupported declaration fails the build with a file
  and line.

### Former phase 8: editor polish and flow visualisation

Goal: the editor feels like a tool rather than a prototype. Blueprint's
readability comes largely from motion: you can see execution move through a
graph, and see which path just fired.

Runs after Phase 3, not before it. Deliverables 3 and 4 consume the exec
traversal records that `DBG-7` creates, and `DBG-7` lands in Phase 3. Ordering
this phase earlier would mean either building traversal recording twice or
blocking on it. An earlier draft of this file had the order reversed, on the
weaker argument that both phases edit `editor/editor.c` heavily; so does Phase
3, and one worker at a time makes that moot.

Estimate: 3 to 5 days.

Covers: `DBG-15`, `DBG-16`, `UI-44` through `UI-51`.

Deliverables:

1. Traversal records gain a run timestamp and activation ordinal, bounded with
   oldest-first discard. `DBG-15`, `DBG-16`.
2. Docking landed in Phase 3, which had to create these panels anyway. This
   phase inherits them.
3. Marker dots travelling along traversed exec edges, source to target, with
   spacing and speed constant in canvas space so zoom does not change apparent
   flow rate. `UI-44`.
4. Edge heat: recently traversed edges thicken and brighten, then decay.
   `UI-45`.

   Unreal's exec-flow envelope is attack, sustain, release, and its defaults are
   worth copying rather than inventing. From `GraphEditorSettings.cpp`: attack
   colour (1.0, 0.05, 0.0) at thickness 6.0, held 0.3s; decay over 0.4s with
   exponent 1.8; sustain colour (1.0, 0.7, 0.4) at thickness 3.5, held 0.4s;
   release over 1.5s with exponent 1.4 to colour (0.5, 0.5, 0.5) at thickness
   1.5. Resting wire thickness is 1.5 for data and 2.5 for exec. The exponents
   are what make the fade read as motion rather than a linear ramp.
5. Node activation highlight with decay. `UI-46`.
6. Data pin change flash. `UI-47`.
7. One interpolation helper used by all four, driven by elapsed seconds, with
   decay expressed as a time constant rather than a per-frame multiplier.
   `UI-48`, `UI-50`.
8. Visualisation reads traversal records only, with no evaluator callback.
   `UI-49`.
9. Multi-selection drag needs no work. `UI-51` was already satisfied and was
   verified manually on 2026-08-18 against d1ae2ff; see
   docs/manual-verification.md. It was listed as a deliverable here before
   anyone checked whether it already worked.

Acceptance criteria:

- `UI-48`: the same run rendered at 30, 60 and 144 frames per second produces
  the same visual state at the same elapsed time. Test the interpolation helper
  headlessly by feeding it a fixed sequence of delta times; a per-frame
  multiplier fails this and a time constant passes.
- `UI-50`: stopping a run mid-fade settles every animated value to resting
  within its time constant, and no value is left part-way.
- `UI-49`: `grep` finds no evaluator callback or execution hook in the render
  path, and disabling visualisation entirely changes no run's step count.
- `DBG-16`: a run that traverses more edges than the record budget keeps the
  most recent ones and discards the oldest, with no growth in memory.
- `UI-44`: dot spacing and travel speed measured in canvas units are unchanged
  across a 4x zoom range.
- Manual `UI-45`, `UI-46`, `UI-47`: run the flow demo and confirm the decay
  reads as a fade rather than a step change.

## Not scheduled

From the `Deferred` section of `docs/spec.md`, in the order they would most
likely be picked up:

1. ECS binding as a separate module: entity and component Get, Set, Add,
   Remove, and Query nodes over the same descriptor API.
2. Blueprint classes, inheritance, interfaces, and class defaults.
3. Event dispatchers and delegate binding.
4. Timeline nodes and curve authoring.
5. Bytecode compilation, if profiling shows the interpreter is the bottleneck.
6. A node pulse when a breakpoint pauses on it, which needs the breakpoints
   landing in Phase 3.
