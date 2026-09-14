# Space jobs: game design draft

Draft 0.1, September 11, 2026. This document defines the proposed game before implementation. It records confirmed direction, recommends concrete rules, and identifies what we need to learn through play. Timings and content counts are starting hypotheses.

## The game we are making

You are a freelance pilot taking jobs in a region of space choked with fog, rock, and wreckage. You leave a safe dock with a working ship and an idea. Each job takes you somewhere dangerous and supplies parts that let you develop that idea. You return with a stranger, more capable machine, rebuild it in safety, and decide what it can handle next.

The central promise is **"I built this ship, and look what it can do."**

The player moves and aims independently. Combat demands active piloting, while the ship can contain weapons and systems that react automatically to those actions. Exploration, combat, and customization should support the same decisions. A weapon that is useful in a narrow passage should influence which route the player takes and which job they accept.

## Confirmed direction

The following decisions come from the design discussion:

- Twin stick shooting in space, with jobs giving the player destinations and purpose.
- Dense fog that limits current visibility, with cave-like passages and chambers.
- Bursts of panic and action followed by genuine safety for upgrading.
- Several jobs in one connected region per run, with safe stops and a choice to cash out.
- Customization centered on combinations of weapon behaviors and interacting ship systems.
- New starting options and module blueprints carry between runs. Power is rebuilt within each run.
- Player expression comes first. Player actions should produce understandable reactions, and the player should feel increasingly powerful.

All specific mechanics below are proposals. Freeform construction of the ship's shape is not part of this draft's customization model.

## How we treat the player

Treat the player's attention, ideas, and accomplishments as the center of the experience. The world should give them material to act on and evidence that their choices mattered.

Let combinations become excessive. When the player assembles something unusually strong, give them enemies and situations on which to enjoy it. Increase the stakes through optional jobs, unfamiliar threats, and more demanding spaces. Do not immediately increase every enemy's health to cancel the upgrade.

Respect intent. Steering should feel responsive, aiming should feel precise, and an attempted action should receive feedback even when unavailable. Show the player which system caused an effect. A spectacular chain reaction is more satisfying when they recognize their own construction in it.

Make experimentation inexpensive. Refitting owned parts is free at docks. A player can test a change, undo it, and try again without spending their job reward each time. Rewards should help them pursue an idea, including an unusual one.

Consequences need not punish. A wake in the fog, an enemy pushed aside, a newly visible passage, and a docking arm unfolding are all ways to acknowledge the player. Reserve danger for consequences that create worthwhile decisions.

## The primary loop

**Choose a job → prepare the ship → navigate and investigate → complete the objective → reach a dock → collect and rebuild → take another job or cash out.**

| Moment | Player decision | What the game gives back |
| --- | --- | --- |
| At the dock | Which job suits my ship or offers the part I want? | A clear objective, destination region, known hazards, and reward preview. |
| Preparing | What combination do I want to try? | Free rearrangement, a working preview, and a nearby test range. |
| Traveling | Which route, signal, or shortcut is worth investigating? | Useful discoveries, readable landmarks, and encounters shaped by the route. |
| At the objective | How can my build solve this situation? | Enemies, cover, obstacles, and objective objects that respond to the ship. |
| Returning | Do I take the direct route or one more detour? | Familiar geography, visible evidence of earlier actions, and optional salvage. |
| Rebuilding | What did I learn, and what can this new part enable? | A meaningful reward, recovery, and time to experiment. |
| Leaving the dock | Cash out, choose a manageable job, or pursue a larger prize? | A voluntary change in stakes. |

A run begins when the player enters a new region and ends when they cash out at a dock or lose their ship. Start by aiming for roughly 20 to 30 minutes with three to five jobs. An early successful extraction is valid. The eventual duration should follow how much exploration and tinkering players enjoy.

The region has a finite set of jobs and discoveries. Completing ordinary jobs can reveal an optional headline contract that gives a strong build a final challenge. It should be possible to leave satisfied without accepting it.

Accept one job at a time in the first version. Keep its objective visible and concise. Players can abandon it at a dock and choose another without a permanent penalty. Abandonment forfeits its completion reward but preserves ordinary salvage already collected.

## The rhythm of a job

Use the route and the objective to build tension. Avoid a universal survival timer that continues escalating while the player is inspecting parts.

1. Leave safety with a plan and a visible destination bearing.
2. Travel through a short stretch that gives the ship room to move and the player room to look.
3. Notice evidence of danger before committing to it.
4. Enter a fight or objective complication that creates a short period of panic.
5. Finish the encounter and get time to inspect what happened and collect its rewards.
6. Reach safety, recover, and spend as much time rebuilding as desired.

A starting target is 30 to 90 seconds of intense activity inside a job lasting several minutes. These are pacing hypotheses, not repeating wave schedules. A quiet pocket may contain a discovery or a distant threat rather than another immediate fight.

Docks are reliably safe. Docking requires reaching a visible protected entrance; it is not an inventory button that grants invulnerability anywhere. Once inside, threats cannot damage the ship and job pressure stops. There are no surprise dock invasions in this design.

Enemy escalation follows destination, chosen contract, and explicit local events. Cleared paths generally stay cleared. Job descriptions disclose exceptions such as a recovery operation that attracts pursuit. The player should sometimes return through a place that frightened them earlier and dominate its remaining threats.

## Space that is worth navigating

Build the region from recognizable chambers connected by passages. Asteroid interiors, wreck fields, broken industrial structures, and dense particulate clouds can give space a cave-like shape while preserving the feeling of piloting a ship.

Each job route should offer a consequential choice. A wide route permits maneuvering but exposes the player to patrols. A narrow shortcut favors bank shots and close combat. A side chamber contains salvage that may improve the next refit. These choices must appear in the world and its map, with enough information to form a plan.

Use loops and shortcuts so returning does not require repeating a long empty corridor. Place identifiable landmarks near junctions. Fog can make a destination mysterious without making navigation tedious.

Start with a deliberately authored region to test these relationships. Procedural generation becomes useful once we know which room shapes, routes, and discoveries produce good decisions.

### Fog and readable danger

Keep three kinds of information distinct:

- Current vision reveals nearby terrain, actors, projectiles, and effects. Dense fog shortens this range; solid obstacles block it.
- Map memory retains explored terrain and landmarks. It does not track enemies the player can no longer see.
- Sensors briefly reveal contacts or directions. A contact is partial information, not permission for ordinary weapons to lock through solid walls.

The ship disturbs the fog. Thrusters carve a fading wake, shots light short paths, and blasts open brief clearings. These effects help the player read their movement and fire. A scanner pulse can reveal a fork or silhouette before the player enters it.

Make information part of builds. One ship can extend visibility, another can use remote beacons, and another can exploit narrow corridors with ricochets. The basic ship still needs enough visibility and maneuverability to complete ordinary jobs.

Fog may hide an attack's source, but a damaging attack must provide a readable cue and time for a feasible response. Use projectile entry, a directional warning, a charging silhouette, or a visible disturbance. Pair sound with a visual cue. Enemies do not silently spawn inside the player's immediate safety margin.

Test warning distance against actual ship speed and attack speed. Increasing fog density is not an acceptable shortcut to difficulty if it removes the player's opportunity to act.

## Jobs that support different ships

Jobs create situations the player can approach with their build. Give each job a short objective, known threat information, a reward preview, and a visible completion condition. Optional bonuses can encourage approaches without making an entire build invalid.

| Job family | Basic objective | Choices it creates | Possible complication |
| --- | --- | --- | --- |
| Recover | Retrieve an object from a wreck and deliver it to a dock. | Fight through the entrance, find another approach, or open a shortcut. | The recovered object emits a visible signal that attracts nearby hunters. |
| Hunt | Find and destroy a marked threat, then reach a dock to settle payment. | Follow clues, ambush it in favorable terrain, or engage its escorts first. | The target relocates after a clear warning, creating a pursuit. |
| Survey | Scan marked sites and return the data. | Plan an efficient route, use sensors, and decide which fights to avoid. | Activating a site reveals local threats and draws a bounded response. |

Use recovery and hunting for the first design test. Surveying can follow once fog and navigation support interesting decisions by themselves.

Show objective progress in the world. A recovered object visibly attaches to the ship; the job marker changes to the delivery dock. A hunted target breaks apart and stops affecting its surroundings. Completion should be obvious before a reward panel appears.

## Building the ship

The ship combines a chassis, weapons, and ship systems. The chassis supplies a starting identity and fitting capacity. Weapons define what attacks do. Ship systems connect piloting, sensing, and weapon behavior.

Start with one chassis, one primary weapon recipe, and one utility action. The chassis has a few explicit places for systems. Let installed equipment alter the silhouette, exhaust, and firing appearance so players can recognize their own ship.

### Weapon recipes

Use a short, ordered row of parts with any triggered payload shown beneath its carrier. Players should be able to read the result as a sentence. This is a player-facing fitting interface, separate from the editor's behavior graphs.

Examples of proposed recipes:

- Fire a bolt that ricochets twice.
- Fire a slow projectile that releases a spread of shards on impact.
- Fire a piercing shot that pushes enemies into hazards.
- Deploy a beacon that reveals nearby fog and becomes a target for a support weapon.

The basic pieces are a carrier, modifiers, and an optional event that releases another payload. Each part states what it affects. The fitting preview shows the order, inherited effects, and expected costs. Moving a modifier must have a predictable, visible result.

For the first test, modifiers affect the carrier in their own row. A secondary payload uses its own row of modifiers. An impact trigger releases its payload on the carrier's first valid impact and is then consumed, even if the carrier can ricochet or pierce afterward. Permit one secondary stage initially; richer repetition and event combinations are later design work.

Use energy per shot, firing interval, and fitting space as the initial constraints. Energy regenerates. Show sustained firing behavior in the range so players can distinguish a brief burst from a build that fires continuously. Delay heat, ammunition economies, and multiple reactor resources until one of them solves a demonstrated design problem.

An empty energy reserve delays firing with clear feedback and recovery. It never disables movement or traps the player. Every run begins with a functional weapon, and refitting always allows returning to a functional configuration.

### Systems that connect actions

Ship systems should create recognizable relationships between actions. Candidate examples:

| System idea | Relationship | What the player changes |
| --- | --- | --- |
| Momentum capacitor | Sustained movement charges the next primary shot; firing consumes the charge. | Weave between charged shots or keep firing at ordinary strength. |
| Survey coupler | A scanner pulse marks visible targets for a short period; primary hits on those targets release an extra effect. | Reveal the encounter, then choose where to focus fire. |
| Recoil drive | Firing adds a controlled impulse opposite the aim direction. | Use the weapon to strafe, retreat, or sustain a moving firing pattern. |
| Blast stabilizer | Reduces self-damage and displacement from the ship's own explosives. | Fight closer to the explosions the build produces. |

These are candidate parts, not an approved content list. The first test needs two systems with clearly different implications. Start with movement charging and scan marking to compare piloting and information as sources of power.

Support dramatic builds, while keeping cause and effect readable. Repeated effects need an explicit stopping rule. A part that repeats says how many times; a secondary payload does not silently recreate its own trigger forever.

### The refit experience

At a dock, put the ship, owned parts, and resulting weapon together. Allow direct rearrangement and undo. Show incompatible placements where they occur. Keep a plain-language recipe summary next to the test action.

The test range must demonstrate the effects the player actually uses, including walls for ricochets and movement space for charging. Switching between fitting and testing should be immediate. Save the previous working arrangement so experimentation is easy to reverse.

The player should leave thinking, "I want to see this hit something." That anticipation is part of the primary loop.

## Rewards and power growth

Make completing a job the main source of growth. Ordinary combat and exploration supply salvage and occasional parts. A successful settlement provides enough recovery and purchasing power to make a meaningful change.

For the first test, settlement fully repairs the hull and awards a choice of parts plus spending credits. Rearranging owned parts is free. This guarantees that one bad fight does not consume the entire opportunity to experiment. Revisit the repair economy only if this removes too much tension.

Free repairs occur on successful settlement, so repeatedly touching the dock does not refill the hull. A player who abandons a job can still refit, spend existing credits on repairs, or cash out safely. Do not require a badly damaged ship to complete another contract merely to end its run.

Show reward categories before accepting a job. At settlement, offer at least one option compatible with the current ship and one option that invites a different direction. Provide a useful fallback when a part is unwanted. The first completed job guarantees a behavior-changing choice.

Growth should appear in action. The opening ship handles enemies individually. A few jobs later it can control a passage, collapse a group, reveal threats before they engage, or turn a risky maneuver into an attack. Numerical improvements can support this, but a larger damage number alone does not establish the identity of a build.

Jobs deeper in the region pay more because their known threats demand more from the player. Earlier enemies keep their established properties. Do not scale the whole region to the equipped weapon's damage.

### What survives a run

Confirmed: permanent progression opens choices rather than accumulating combat stats. Each run earns its own powerful ship.

Proposed extraction stakes: cashing out secures discovered blueprints and completed challenges that unlock future starting options. The ship, installed parts, and run credits end with the expedition. Death loses those items and the current expedition's unsecured unlocks; previously secured unlocks remain. A discovery journal can retain information without granting an unearned starting item.

A new module can appear as salvage before its blueprint is secured. Securing the blueprint makes it an available starting option, where appropriate, or unlocks a job reward category that helps the player find it again. Starting loadouts have a fixed budget, so an expanded catalog does not let a new ship equip every unlocked module. Reward previews and directed job choices help players pursue a style as that catalog grows.

The exact point at which an unlock becomes secure still needs discussion. Test extraction banking against securing unlocks at each successful job settlement. The former gives cashing out stronger stakes; the latter protects more experimentation after a good job.

## Every action gets a reaction

Feedback should show input response immediately and show the actual gameplay consequence when it occurs. A missed shot still lights the fog and produces an appropriate impact. It does not need to grant damage or rewards.

| Action | Immediate response | Gameplay consequence where applicable |
| --- | --- | --- |
| Thrust or turn | Exhaust changes, the ship banks, and a wake opens behind it. | Position changes; fitted movement systems build charge. |
| Aim | The weapon mount and aim cue follow input. | The player can line up a shot while moving elsewhere. |
| Fire | Muzzle action, sound, projectile, and visible energy use originate at the weapon. | Shots interact with enemies, fog, walls, and supported objects. |
| Hit an enemy | Directional impact and a readable reaction. | Damage, displacement, or a status change matches the weapon. |
| Break an object | A visible fracture and debris show what broke. | A supported obstruction opens or a hazard changes state. |
| Scan | A pulse spreads from the ship and contacts appear where detected. | A route or target becomes known; fitted systems can mark it. |
| Recover salvage | The pickup moves into the ship and its count updates on arrival. | The inventory gains that item once. |
| Take damage | Hull response and a directional cue identify the source. | The player can judge whether to fight, disengage, or end the run. |
| Fit a part | The relevant mount or system changes and the preview updates. | The next test shot or maneuver behaves differently. |
| Complete a job | The objective changes state and the return destination becomes clear. | Settlement becomes available, with the promised reward. |
| Dock | Guidance lights, clamps, and a quieter soundscape establish arrival. | The ship enters safety and gains access to fitting. |
| Attempt an unavailable action | A local cue explains the missing charge, range, or valid target. | The player knows what must change before the action can work. |

Preserve hierarchy during powerful combinations. Player position, incoming danger, and objective state must remain visible beneath friendly effects. Feedback intensity should reflect the importance of the event. Allow reduced flashes, shake, and motion without removing necessary information.

## A sample run

The player accepts a recovery job offering a weapon modifier. They enter a wreck field with a basic bolt weapon and scanner. At a fork, a pulse reveals a short passage and a larger chamber containing several contacts. They take the passage.

At the wreck, they collect a salvage core. It starts pulsing, and approaching hunters disturb the fog. The player retreats through the passage they already know, picking off the closest pursuer and reaching the dock with damaged hull.

Settlement repairs the ship. The player selects ricochet and buys a movement capacitor. In the range they discover that a charged bank shot covers the entire test corridor. They rearrange a part, test again, and leave for a hunt with a specific plan.

The hunted ship has escorts. The player draws them into a narrow junction, builds charge while circling cover, and fires into the wall. The ricochet clears the escorts. A place that forced retreat on the previous job now suits the build.

At the next dock, an impact payload suggests another experiment. A larger contract is available deeper in the fog. The player can secure the run's discoveries now, or take their new weapon out for one more job.

This is an illustrative scenario. It describes the experience the proposed rules should produce, not a scripted sequence every player must follow.

## The smallest useful design test

Build this only after refining the draft's open decisions. Aim for an 8 to 12 minute session that includes the whole loop:

- One authored region with a safe dock, a branching route, a shortcut, two job sites, and one optional salvage pocket.
- Responsive movement, independent aim, primary fire, and a scanner utility.
- One chassis, one weapon recipe, and a small pool of about eight parts that can produce at least three visibly different builds.
- Movement charging and scan marking as alternative ship systems.
- Recovery and hunt jobs, a hunter that closes distance with an announced lunge, a sentry that charges a directional shot, and a stronger hunt target that combines familiar threats.
- Current visibility, remembered terrain, scanner contacts, and readable attacks at the fog boundary.
- Job settlement, hull recovery, free refitting, a range, a second job, voluntary extraction, and defeat.
- One example of an extracted unlock becoming a new option on the following run.

Use this test to answer specific questions:

| Question | Evidence to look for |
| --- | --- |
| Does the first reward change play? | By the first refit, ideally within four minutes, the player can demonstrate a new behavior and explain its cause. |
| Does customization support expression? | Different players make different functional ships from the same pool, and at least one deliberately rearranges a build after testing it. |
| Do jobs support builds? | A player chooses a job or route because of the weapon they made. |
| Does fog create anticipation? | Players use cues or scanning to prepare; after damage they can identify a cue and a possible response. |
| Does the ship feel more powerful? | A later encounter with a familiar threat requires less effort or admits a new solution, without changing that threat's stats. |
| Is the pacing working? | Players recognize safety at the dock, willingly experiment, and leave wanting to try the result. |
| Is cashing out a real choice? | Players can explain what they secure by leaving and what tempts them to continue. |

If the test misses these outcomes, revise the loop before adding more biomes, currencies, or upgrade trees. Full procedural regions, freeform hull construction, crew management, faction simulation, crafting trees, and an extensive material simulation are outside this first test.

## Decisions still to make

| Decision | Current recommendation | What it changes |
| --- | --- | --- |
| Can the player's own build kill them? | Clearly marked explosives can cause self-damage, with a forgiving test range and readable previews. Ordinary shots are safe to use near walls. | How dangerous experimentation feels, and whether defensive systems enable otherwise risky builds. |
| When are permanent unlocks secured? | On extraction, initially. Compare against securing them at each settlement. | The stakes of taking one more job and the cost of failure. |
| How much inertia should flight have? | Responsive acceleration and strong braking, with optional systems that create deliberate drift or recoil movement. | Whether narrow navigation feels expressive or obstructive. |
| How long should a successful run last? | Test the short complete loop before committing to the proposed 20 to 30 minutes. | Map size, number of jobs, and how often builds transform. |
| What inhabits this space? | Begin with wrecks, industrial machines, and strange life in the fog as a provisional fiction. | Enemy identities, salvage appearance, sound, and the tone of contracts. |

The next discussion should settle how dangerous player-built weapons are allowed to become. That choice affects fitting, feedback, enemies, and the emotional cost of experimentation.

## Relationship to the existing project

The [arena roadmap](arena-roadmap.md) records the earlier fixed-arena playable and its acceptance work. This document proposes the broader game direction; it does not claim that the new loop exists or close the arena's unfinished checks.

The [gameplay authoring plan](gameplay-authoring-plan.md) describes the editor's authored behavior work. The game's recipes, jobs, rewards, world composition, and tuning should remain authored content. Shared engine work should supply reusable capabilities where this design exposes a concrete gap, consistent with [AGENTS.md](../AGENTS.md).

This is a game design document. A technical plan should follow only once the first design test and its unresolved rules are agreed.

## Reference notes

These notes use the developers' Steam descriptions, checked September 11, 2026. They establish advertised mechanics, not hands-on verification. The design choices in this document are our proposals. The desire for cave-like space and limited vision comes from the brief; the following sources do not establish a shared fog system across these games.

| Reference | What the primary source establishes | What it contributes to this draft |
| --- | --- | --- |
| [Nautical Survival](https://store.steampowered.com/app/2860990/Nautical_Survival/) | An ocean auto-shooter with vessel customization, islands and upgradeable outposts. Its description lists runs of about 20 minutes. | A readable travel, combat, and improvement rhythm. Our job contracts and safe refitting rules are separate proposals. |
| [Noita](https://store.steampowered.com/app/881100/Noita/) | Players combine spells and use them against enemies and a physically simulated environment; death ends the attempt. | Combinations that feel authored by the player, and attacks that can affect the surrounding world. Exact wand mechanics and a full material simulation are not requirements for this game. |
| [Primordialis](https://store.steampowered.com/app/3011360/Primordialis/) | Players assemble creatures from functional cells and explore an ecosystem whose enemies use the same creature-building system. | A constructed identity the player can recognize in motion, with interacting parts that support different ways to play. |
| [Pathogenic](https://store.steampowered.com/app/3808690/Pathogenic/) | A twin stick shooter with collected organelles, chained effects, and branching organic environments. Its description also includes permanent upgrade trees. | Growth through interacting parts and shaped routes through a dense environment. Our confirmed progression uses new options instead of permanent stat accumulation. |
