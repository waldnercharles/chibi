# Starwell

Neon Swarm's successor game. Starwell lives in `projects/arena2d` and replaces
the Sandbox as the start scene. Sandbox stays as a material test scene.

A dead star collapsed into a hollow well of rock, glass, fungus and slag. You
pilot a small salvage ship down the well. Each run is one seeded, procedural
descent through five strata. It ends at the Heart, or at your death. The ship
you finish with is one you built from what you found.

The target is a 60–100 minute run with endless replayability:

- Every run has a new seed.
- The module combinations are open-ended.
- Beating the Heart starts a harder descent (Heat) on a fresh seed, and you keep your ship.

## Pillars

1. **Predictably reactive matter.** Every material obeys a small set of
   consistent rules the player can learn and exploit:
   - fire ignites fuel, oil and fungus;
   - water puts out fire and flashes to steam on lava;
   - lava that cools becomes rock;
   - acid eats soft rock but not metal;
   - cold freezes water into ice that shatters;
   - blasts breach weak rock;
   - arcs chain through water.

   Enemies obey the same rules as the player.
2. **Your ship is your idea.** Weapons are frames with ordered module slots, as
   in Noita's wands. The order matters: modifiers shape the next emitter, and
   triggers carry the rest of the list to the point of impact.
3. **Spectacle that explains.** Every effect tells the player something:
   - light (HRC GI, bounce, caustics, reflections, refraction, lit fog);
   - motion (hit-stop, shake, shockwaves, recoil, banking);
   - matter (debris, liquids, gas, MPM jelly and sand).

   The player, threats and routes stay legible.
4. **Tension and relief.** Travel is eerie, encounters are sharp, and Sanctums
   are safe. Every stratum ends in a boss that gates the way down.

## The run

| Stratum | Depth (world units) | Matter | Signature threat | Boss |
| --- | --- | --- | --- | --- |
| Basalt Throat | 0 – 6,000 | Columnar basalt, fuel gas, oil pools, water seeps | Swarmers, Spitters, wall Barnacles | **The Grinder**, a segmented worm that bores through rock |
| Lumen Grotto | 6,000 – 12,000 | Soft fungal rock, glowing growth, spore gas, acid slime | Jellies (MPM), Sporelings, Lancers | **Mother Bloom**, a jelly heart with spore cannons |
| Glass Refractory | 12,000 – 18,000 | Crystal, glass, ice, cold water | Prism drones, Mirror turrets | **Prism Warden**, a rotating lattice that splits beams |
| Foundry Deep | 18,000 – 24,000 | Slag metal, lava, molten metal, steam | Furnace crawlers, Welders | **Crucible Colossus**, a forge that pours lava |
| The Heart | 24,000 – 27,000 | Void glass, every earlier matter | Echoes of every stratum | **The Heart of the Well**, three phases |

**Sanctums** sit between strata:
- They are sealed chambers where enemies never go.
- Each has a Shop, a Refit bench and a Resonance shrine for the skill tree.
- The seal below a boss arena opens when the boss dies.
- The next Sanctum waits beneath.

**Layout.** The world is about 3,200 units wide and bounded at its sides. Each stratum is a warped network of chambers, shafts and side pockets generated from the run seed.
- A guaranteed main route descends.
- Side pockets hold caches behind weak walls.
- Fuel pockets, pools and nests make encounters.

## Flight and combat

- **Controls:**
  - WASD or the left stick to move;
  - mouse or the right stick to aim;
  - left mouse for the primary hardpoint, right mouse for the secondary;
  - Space to boost;
  - E to use a terminal;
  - Tab for the ship screen (only in Sanctums).
- **Gravity:** the well pulls matter down: liquids, powders, debris and scrap. The ship's drive holds it up, so flight is unchanged.
- **Hull:** it regenerates only in Sanctums or from repair orbs.
- **Shield:** it absorbs hits and recharges after two quiet seconds.
- **Capacitor:** firing drains it, it recharges continuously, and when it's empty you're left maneuvering.
- **Death** ends the run. A recap shows depth, time, kills and the ship.

### Weapon frames and modules

Each hardpoint holds a frame, and the frame sets:
- slots (2–7);
- capacitor draw;
- cast delay;
- recharge after the list wraps;
- spread.

On each cast, the frame reads modules left to right:
- **Emitters** end a cast: Pulse Bolt, Plasma Glob, Rail Needle, Seeker, Scatter Shard, Arc Lance, Bore, Cryo Shard, Acid Flask, Mine and Singularity.
- **Modifiers** apply to the next emitter: Twin, Homing, Ricochet, Pierce, Accelerate, Heavy, Incendiary, Cryo, Corrosive and Split.
- **Carriers** attach the rest of the list to the next emitter:
  - **Trigger** casts it where the emitter hits;
  - **Timer** casts it after 0.4 s;
  - **Burst** casts it where the emitter expires.

Examples:

| Modules | Result |
| --- | --- |
| `Rail · Trigger · Incendiary · Plasma` | Pierces a wall and bursts into fire inside the fuel pocket beyond. |
| `Homing · Twin · Seeker` | Two missiles that hunt. |
| `Timer · Scatter · Cryo · Acid` | A flak shell that freezes a pool, then bursts acid through the ice. |

### Skill tree (Resonance)

Kills, cores and rare pickups give Resonance. Each level grants a point, which you spend at shrines. There are three branches of nine nodes:
- **Pilot:** hull, shield, thrust, boost, dodge.
- **Gunnery:** capacitor, cast delay, crit, extra slot, multicast.
- **Engineer:** scrap magnet, material immunities, cheaper shop, reroll, and matter perks such as "your fire burns hotter".

Nodes have prerequisites. A capstone ends each branch.

### Customization

- Hull paint: primary, trim and engine glow colors, from palettes unlocked by depth.
- Engine plume style.
- Weapon frames and modules.

All of it is changed at the Refit bench.

## Visual direction

- **Palette:** each stratum has a key light and a complementary accent:
  - basalt: amber fire against blue-black;
  - grotto: cyan and magenta bioluminescence;
  - refractory: prismatic white with caustic blue;
  - foundry: orange heat against teal steam;
  - heart: violet void with white-hot rims.
- **Depth:**
  - Parallax nebula and silhouette layers are authored per stratum as material graphs.
  - Lit fog shows shafts.
  - Dust shows only where light reaches it.
- **Light:**
  - Every emitter lights the world through HRC: muzzle flash, projectiles, fire, lava, fungus.
  - Water throws caustics.
  - Glass refracts.
  - Pools and crystal reflect.
- **Motion:**
  - hit-stop on heavy hits and kills;
  - trauma-based camera shake;
  - shockwave distortion on blasts;
  - heat haze over fire and lava;
  - chromatic pulses on damage;
  - squash, bank and recoil on the ship;
  - animated UI transitions.
- **Art:** it is procedural. Ships and enemies are SDF silhouettes with material graphs (panel lines, emissive seams, damage). Terrain materials are procedural per stratum.

## Engine capabilities this needs

These are shared, optional capabilities; the game rules stay authored.

**Gameplay runtime**
- Seeded RNG and a run seed.
- `hash` and `noise` in behavior expressions.
- Sample Field and Terrain.
- Time scale and hit-stop.
- Camera trauma.
- Add and Remove Component on any entity.
- Restart from a behavior.
- A chain/attach follow for segments and turrets.
- Scatter placement that streams with terrain.

**Matter**
- Gravity for fluids, debris and bodies, with a per-body scale.
- Data-driven fluid substances and a reaction table.
- Acid against terrain.
- Deposition that adds cells to a mask.
- Emit Fluid and Fluid At nodes.
- MLS-MPM for liquids, granular matter and elastic jelly.

**Rendering**
- Post FX: vignette, chromatic aberration, grain, grading, flash, shockwave and heat distortion.
- Caustics.
- Refraction for solid graph materials.
- Liquid-surface reflections.
- Material-graph parallax backdrops.
- Hit flash.

## Status

The status of each capability and content piece is kept in the Progress
section below and updated as work lands.

## Progress

- [ ] Engine: gameplay runtime capabilities
- [ ] Engine: matter (gravity, substances, reactions, deposition, MPM)
- [ ] Engine: rendering spectacle
- [ ] Content: ship, weapons and modules
- [ ] Content: strata terrain, materials and backdrops
- [ ] Content: enemies and bosses
- [ ] Content: Sanctums, shop, refit, skill tree
- [ ] Content: title, HUD, death and victory screens
- [ ] Tuning pass and full-run playtest
