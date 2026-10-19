# Examples

Chibi keeps no separate example projects. Studio and these docs teach the
engine, and every project to learn from is checked in beside the code it
exercises, so it migrates and is tested with the engine.

Open any of them in Studio:

```sh
make studio-run STUDIO_SOURCE=/absolute/path/to/chibi-studio PROJECT_ROOT=test/fixtures/feature-coverage
```

| To see | Open |
| --- | --- |
| Every reflected component, native function and event in a running scene | `test/fixtures/feature-coverage`: one scene per area (Audio, Entities, Flow, Input, Materials, Particles, Physics2D, Physics3D, Rendering3D, Rhythm, Save, Tilemap, Ui and more). Its `coverage-gaps.txt` lists what no scene uses yet, with reasons. |
| A small playable game: movement, live terrain cuts, save and load, a HUD | `test/fixtures/terrain-lab` |
| 3D rooms and shapes | `test/fixtures/scene3d` |
| 2.5D HRC lighting | `test/fixtures/courtyard` |
| Sprite frames | `test/fixtures/sprites` |
| A large authored project: RmlUi documents, behaviors, virtual lists | Studio's own `projects/studio` in [chibi-studio](https://github.com/waldnercharles/chibi-studio); `projects/starter` is what a new project starts from |
| One UI widget by itself, with a log of its actions | Studio's Canvas: open a ui asset |

Start a project with the controller's `new_project` operation, which writes
`project.kdl` and an empty start scene. Publish a game's browser player and
`/dev/` editor with the game web workflow, copied into the game repository as
`.github/workflows/web.yml`.

No checked-in project saves a declarative model yet.

## Retired repositories

On 2026-10-17 the separate example and game repositories were retired and
archived. Their Terrain Lab moved here as `test/fixtures/terrain-lab`; nothing
else from them is migrated, and no engine check reads them.
