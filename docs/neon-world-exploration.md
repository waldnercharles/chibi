# Neon Swarm world exploration

Design under refinement. The experimental implementation was reverted on
September 22, 2026 after the visual direction was rejected. The ambition remains,
but delivery
will proceed one feature at a time. The feature sequence is a proposal for
discussion, not authorization to implement the whole document.

Worktree: `.worktrees/neon-world-exploration`.
Branch: `cwaldner/neon-world-exploration`.
Baseline: `41bc4586`, a snapshot of existing workspace source changes. Creating
that snapshot did not validate the inherited implementation.

This direction supersedes the earlier Space Jobs draft for Neon Swarm. Quests
remain a possible later feature. Contracts, extraction, and permanent unlocks
are not the progression model for this first version.

## How we will work

Select one small change, describe exactly what the player should experience,
implement it, then stop for a hands-on review. Continue only after the user
accepts the result and chooses the next feature. A working build and passing
tests establish correctness; they do not establish that the game looks or feels
right.

Before implementing a feature, agree on:

- The starting scene and the specific behavior or visual being changed.
- A short player action that demonstrates the result.
- References and concrete visual criteria when appearance is involved.
- What belongs in this change and what belongs in a later feature.
- The evidence needed to judge it, including a moving capture where relevant.

Keep each change independently reviewable. If it needs a separate new system,
split that dependency into its own feature and discuss the sequence first.
Do not turn a cave experiment into a simultaneous rewrite of enemies, weapons,
shops, lighting, and progression. If a result is rejected, revise that feature
before advancing. Keep the last accepted version available for comparison.

The worktree has been restored to the original `NeonSwarmSandbox`, currently
named `Sandbox`. It provides the starting point for the existing ship, controls,
camera, lighting, and material interaction. The next feature and its visual
references still need agreement.

## The game

Pilot a ship through connected cave systems, with room for other environments
and remnants of a shattered megastructure later.
Explore an eerie, dangerous world, discover experimental weapon components,
reshape its materials, and defeat regional bosses. Safe docks provide relief and
space to turn discoveries into new weapon combinations. A clever interaction can
transform a difficult encounter into an overwhelming victory.

Flight and gameplay remain on a 2D plane. The world should have visual depth and
scale through procedural surfaces, global illumination, illuminated fog,
parallax, and selected perspective effects. Rendering spectacle is part of the
game's identity. The ship, traversable spaces, and approaching danger must remain
legible as the camera moves and effects overlap.

## Agreed decisions

| Decision | Consequence |
| --- | --- |
| Material interaction and exploration are the initial pillars. | Weapons change routes and hazards as well as damaging enemies. Discoveries invite experiments. |
| The game is a roguelike with a full restart on death. | Death resets equipment, salvage, boss progress, discoveries, and world modifications for the run. There is no sanctuary recovery or retained power in this version. |
| Changes persist throughout a living run. | Returning preserves breaches, collected items, defeated enemies, and other meaningful state. Suspend/resume saving comes after proving this within a session. |
| The main world has recognizable regional geography and procedural interiors. | Generation builds substantial, purposeful locations and connected routes. Distant secrets and extensions can follow. |
| All playable regions connect through seamless flight. | Generation, loading, and unloading happen during play. Region boundaries introduce no loading screen or required travel transition. |
| Distant simulation pauses while retaining its state. | Detailed simulation stays near relevant activity. The active area extends beyond the camera; looking away must not stop a nearby attack or explosion. |
| Progression branches through regional bosses. | Several bosses become available at once. Their victories open deeper regions and ultimately a final encounter. Optional bosses reward exploration. |
| Strong combinations and creative solutions are welcome. | Bosses obey consistent rules. Ordinary weapons remain viable through skilled flight and learning attacks. |
| Encounters are dangerous, travel is eerie, and docks are safe. | Danger belongs to locations and disturbances. Docks provide repairs, weapon and ship upgrades, refitting, and a protected range. The first slice starts with a small module shop. |
| Refitting happens at safe zones. | Discoveries create a reason to return, test, rearrange, and venture out again. |
| Weapons and environmental reactions can hurt the player outside sanctuary. | Experimentation has consequences during an expedition. The dock and range remain reliably safe. |
| One initial weapon has three ordered module slots and a small component catalog. | The slice must prove behavior composition and meaningful order. Finding a module changes what a shot can do. |
| A rechargeable capacitor limits weapon use. | Components affect energy cost and firing delay. Exhausting the capacitor leaves the player maneuvering while it recharges. Ordinary flight has no fuel restriction in the slice. |
| Gameplay structures outside sanctuary are destructible with differing resistance. | Basic shots can breach weak seams; stronger combinations open more direct routes. Background depth remains visually distinct from reachable geometry. |
| Combat presentation preserves mystery. | No health bars or floating combat text. Bosses do not disclose a complete description of their health, resistances, or attacks. |
| The initial performance target is desktop 1440p at 60 fps. | Measure complete gameplay frames during flight, firing, fog, destruction, and streaming. This is a target, not a current result. |

## Mystery and feedback

Let the player learn by observing and trying things. A boss need not announce its
name, remaining health, resistance table, or next phase. Its identity can emerge
from its location, silhouette, movement, machinery, sound, and effects on the
surrounding material.

Proposed presentation defaults for the first slice:

- No health bars for the player, ordinary enemies, or bosses; no floating damage
  numbers, overhead enemy labels, or explanatory combat overlays.
- Impacts, visible fractures, armor changes, altered movement, light, and sound
  communicate consequences. A failed attack should produce an observable response
  without printing a reason over the target.
- Charging weapons, moving structures, projectiles, and disturbed fog provide
  opportunities to react. These cues need not reveal the complete attack rule.
- Show the player's hull condition and capacitor state through ship appearance,
  integrated light, firing response, and sound. Test whether players can judge
  danger and recharge without a numeric combat display.
- Dock screens use concise component names, costs, and fitting controls. The
  range demonstrates interactions. Avoid paragraphs explaining combinations,
  secrets, or boss solutions.
- Reward experiments that exploit terrain, material interactions, or weapon
  composition. Resistances should be consistent and discoverable through play.

Specific cues and thresholds are tuning hypotheses to evaluate in play. Mystery
is an explicit acceptance criterion alongside control response and readability.

## World generation and streaming

Generate the broad relationships between regions, landmarks, sanctuaries, routes,
and boss locations before filling in local geometry. Start with cave chambers,
connecting passages, and alternative routes. Shape passages deliberately, then
use procedural detail for irregular surfaces and material variation. Repeated
random debris is not an acceptable substitute for a convincing environment.

Wave Function Collapse is a candidate for compatible local structural assembly.
Compare it against simpler module placement on representative ruins. It does
not replace the overall progression layout or traversal checks. Its use is an
engineering decision to justify with resulting worlds and generation cost.

The proposed streaming contract is:

- A run seed and stable location identities determine unmodified content.
- Generate and prepare content ahead of expected movement, including boost and
  abrupt changes of direction. Keep loading work within the frame budget.
- Only nearby terrain, physics, and detailed simulation need to be active.
- Preserve modified terrain, meaningful actors, loot state, and boss state when
  content unloads. Returning must not regenerate collected rewards or heal walls.
- Active interactions can keep areas awake beyond the visible camera region.
  Cosmetic particles can expire without retaining every spark indefinitely.
- Bound resident work and storage; stream cold run state separately from active
  simulation. Choose the storage representation during implementation.
- Region and chunk boundaries must preserve geometry, collision, material
  interaction, and visual continuity. Generation constraints must agree across
  independently prepared areas.

These are eventual requirements. A small environment study can use a limited
test area while its appearance and traversal are being judged. That study must
be identified as such; it does not satisfy the seamless-world requirement.
Unbounded noise alone also does not establish a world worth exploring.

## The first playable slice

One starting sanctuary opens into a large cave chamber. The
player leaves with a simple weapon and chooses between
branching passages. Weak material, unusual illumination, or machinery hints
at a concealed space. Breaching it reveals a component or useful material
interaction. Local threats make reaching it dangerous.

The player returns through their altered route and refits at the dock. The range
lets them test a combination before taking it into danger. A regional boss
provides a reason to explore, prepare, and experiment. Defeating it demonstrates
the first progression milestone; dying restarts the run.

Slice content is one sanctuary, one principal cave system, alternative routes,
a concealed discovery, a small module catalog, local threats, and one regional
boss. Later milestones expand branching into multiple regions and bosses.

The first weapon catalog must include an interaction where composition changes
when or where an effect occurs. A penetrating carrier that releases an ignition
payload inside a material pocket is one candidate. The exact catalog and recipe
semantics should be established in the range prototype before expanding content.

## Proposed first steps

The first playable slice above describes where these features should eventually
lead. It is too broad to serve as one implementation task. Choose and review
each step separately; adjust the order as we learn.

| Step | Single outcome to review | Evidence |
| --- | --- | --- |
| Establish the reference | Choose the starting scene and an art direction for one cave surface. | Inspect the existing scene together. Select a few visual references and identify the specific qualities to use. No game changes yet. |
| One cave surface | One section of rock has an intentional silhouette, material, scale, and lighting response. | Fly beside it and fire near it in the reference scene. Compare the moving result with the agreed references. |
| One passage | Two small spaces connect through a passage that feels good to navigate. | Fly both ways at normal speed and while boosting. Judge clearance, camera behavior, and the view ahead. |
| One breach | The basic weapon opens a useful route through one weak section. | Shoot it, fly through the opening, and return. The visible opening and collision must agree. |
| Extend the accepted terrain | The accepted cave treatment continues across streamed boundaries. | Fly beyond the original test area, reverse direction, and return to the breach. Check continuity, retained damage, frame times, and resident storage. |

These steps are provisional. Do not begin the first cave implementation until
the reference and its visual criteria are agreed. A feature can be smaller than
one table row if the row still contains too many independent decisions.

Later work stays in the backlog until it is selected: one enemy with one attack;
one discoverable reward; one weapon modifier; one interaction between two
ordered modifiers; capacitor behavior; sanctuary protection; refitting; a shop;
death and full restart; a boss attack; and boss progression. Refine only the next
selected item into an implementation brief. Names, catalogs, quantities, and
balance from the rejected experiment are not automatically part of that brief.

## Next feature brief

Current activity: refine the design and choose the reference. Implementation is
paused.

| Question | Current position |
| --- | --- |
| Which scene do we start from? | The restored `Sandbox`, formerly `NeonSwarmSandbox`. |
| What should the first new result be? | One convincing cave surface is proposed. Confirm after choosing the reference. |
| What should it look like? | Unresolved. Choose references before generating or implementing visuals. |
| How will we judge it? | Fly alongside it and fire near it. Inspect silhouette, material response, depth, brightness, and readability in motion. |
| What is deliberately deferred? | Additional areas, enemies, bosses, weapon systems, shops, and a complete run loop. |

This section should hold only the next agreed feature. Replace it after that
feature is accepted, keeping the decision and evidence in a short delivery note.

## Rendering and material experiments

Start with an intended image and movement experience. Choose techniques to
produce that result. Adding GI, fog, procedural noise, parallax, or more geometry
does not by itself improve the art direction. The rejected experiments remain
examples to compare against, not accepted visual references.

Evaluate depth in a moving scene: fly past foreground structures, expose distant
architecture, fire through fog, and open a breach. Perspective scaling alone does
not provide depth-aware HRC transport. Test layer occlusion and lighting instead
of assuming that a static screenshot proves compatibility.

The separate GI research checkout contains persistent medium work to inspect and
evaluate for reuse. Its earlier measurements do not establish the complete
exploration game's performance. Integrate only relevant changes after checking
the branch differences and measuring the resulting scene. The first attempted
integration was removed after visual review. Keep the existing simulated gas
that responds to bodies and projectiles. Do not add a screen-filling decorative
fog layer that brightens the whole image or ignores the ship.

Different simulated materials are a longer-term part of the direction. MPM is a
candidate for localized deforming, flowing, or phase-changing material. Begin
with a bounded encounter and compare its behavior, rendering, collision coupling,
streaming state, and complete-frame cost against the existing material systems.
It is not a required replacement for all terrain or a prerequisite for the first
cave study. Select the initial material experiment when that work begins.

## Implementation boundaries and verification

Shared native modules own reusable capabilities such as streaming, material
operations, runtime state storage, and rendering. Authored assets own regional
composition, rules, bosses, weapon effects, visual choices, and tuning. Keep
quests, combat, and progression optional for scenes that do not use them.

Use Chibi MCP for authored content after confirming its project root and target
scene belong to this worktree. The currently connected server was last observed
pointing at the original checkout; it must not silently author this branch's
assets through that other project. Search and inspect first, then build,
validate, and preview. Use Behavior Expressions for small calculations and
explicit graph structure for larger sequencing and state.

Run `make build` and `make test` as appropriate to implementation changes. Add
focused checks for meaningful contracts such as deterministic generation,
streaming restoration, boundary collisions, loot duplication, weapon ordering,
and death reset. Validate reusable engine work in another scene without this
game's combat and progression. Direct editor and game interaction must accompany
authored or presentation changes.

Record representative complete-frame timings, including streaming transitions,
with fixed settings and repeatable inputs. Record visual quality alongside
performance. Keep test results, GPU measurements, and hardware coverage explicit.
The design document alone does not establish any of those results.

## References

- [Current material sandbox](neon-swarm-sandbox.md)
- [Material engine and authored fields](material-engine.md)
- [Material simulation and HRC research](material-simulation-and-hrc-research.md)
- [Chibi authoring workflow](mcp.md)
- [Wave Function Collapse](https://github.com/mxgmn/WaveFunctionCollapse)
- [MLS-MPM and CPIC](https://yuanming.taichi.graphics/publication/2018-mlsmpm/)

## Current implementation

The exploration implementation has been reverted to snapshot `41bc4586`.
Shared engine code, tooling, tests, and project configuration match that
pre-experiment baseline. Startup selects `Sandbox` again. The experimental scenes,
assets, streaming modules, and gameplay checks have been removed from the active
worktree. The main checkout was not changed by this rollback.

This design document and the link from the earlier game-design draft remain.
The removed experiment, its patch, and its historical validation notes are
archived locally under `.chibi/revert-archives/20260922-080054/`. They are available
for deliberate inspection or reuse when a feature is selected; they are not
loaded by the game.

Stable 1440p/60 for the intended exploration game remains unproven. Historical
measurements of the rejected experiment do not establish that target.
