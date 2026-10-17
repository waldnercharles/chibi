# Terrain Lab

A small native 2D excavation playground and the engine's terrain save/load
fixture. A mint flag marks the boundary between 128-cell terrain chunks. The
cave on the left is part of the authored field; the circular cuts you make are
live terrain edits. The amber flag at x=256 marks an untouched reference patch.

All game behavior is in `content/behaviors/Explorer.chibi`. The terrain recipe is
`content/libraries/QuarryShapes.chibi`, connected by `fields/QuarryField.kdl`.
Dynamic Body physics and raycasts use the generated terrain colliders, including
holes restored from a save.

| Action | Key |
|---|---|
| Move | A / D |
| Jump | Space |
| Dig the outlined circle ahead | F |
| Save current terrain | S |
| Load saved terrain | L |
| Respawn without resetting terrain | R |

Save and load are explicit. Startup never digs, saves or loads. The game owns the
stable identity `terrain-lab-quarry-v1`; the native player's `--save` path chooses
the durable storage namespace. The HUD reports pending operations, success and
storage failures. Loading moves the explorer back to the starting ledge so a
restored wall cannot trap the player.

## Repeatable restart check

1. Start the native player with a writable `--save` path. Let the explorer land.
2. Tap F once without moving. Its circle overlaps both sides of the mint flag.
3. Tap S and wait for "Terrain saved". The amber reference patch stays intact.
4. Close the player. Start it again with exactly the same `--save` path.
5. Confirm fresh ground appears at the mint flag until you tap L.
6. Tap L and wait for "Saved excavation loaded". The seam cut returns, while the
   amber reference patch remains intact. Walk into the cut and jump to check
   that collision follows the restored cells.

The initial explorer centre is (-22, -12), facing right. Once grounded, F carves a
radius-24 circle centred near (0, 2). The terrain spans x=-384..384 at cell size
1, so x=0 is an exact 128-cell chunk boundary. The reference patch near (256, 0)
is outside that cut. R respawns at (-42, -16) and preserves the edits.

## Build and run

Run these commands from the engine repository root after `make package-sdk`
(installed at `dist/sdk`). Use the same installed SDK for compiler and player,
and keep the generated resource manifest beside the prepared artifact:

```sh
mkdir -p /tmp/chibi-terrain-lab
dist/sdk/bin/chibi-compile --project-root test/fixtures/terrain-lab --output /tmp/chibi-terrain-lab/terrain-lab.chibip
dist/sdk/bin/chibi-player /tmp/chibi-terrain-lab/terrain-lab.chibip --resource-root test/fixtures/terrain-lab \
  --shader-dir dist/sdk/share/chibi/shaders --save /tmp/chibi-terrain-lab/terrain-lab.save --backend sljit
```

Run the player command again with the same save path for the restart check
above. Browser persistence is not supported by these native storage calls.

## Checked acceptance

`python3 tools/test_policy.py tool terrain-playable` drives these authored
controls on VM and SLJIT, writes a save, then starts a fresh reader process. It
checks pristine terrain before L, restored seam edits and collision afterward,
and an unchanged reference patch. `--terrain-playable-render` runs the installed
player on an artifact prepared from this project for four hidden 640x360 frames.
Terrain saves exclude the explorer and detached fragments.
