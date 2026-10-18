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
| Every reflected component, native function and event in a running scene | [`test/fixtures/feature-coverage`](../test/fixtures/feature-coverage/README.md): one scene per area (Audio, Entities, Flow, Input, Materials, Particles, Physics2D, Physics3D, Rendering3D, Rhythm, Save, Tilemap, Ui and more). [`coverage-gaps.txt`](../test/fixtures/feature-coverage/coverage-gaps.txt) lists what no scene uses yet, with reasons. |
| A small playable game: movement, live terrain cuts, save and load, a HUD | [`test/fixtures/terrain-lab`](../test/fixtures/terrain-lab/README.md) |
| 3D rooms and shapes | `test/fixtures/scene3d` |
| 2.5D HRC lighting | [`test/fixtures/courtyard`](../test/fixtures/courtyard/README.md) |
| Sprite frames | [`test/fixtures/sprites`](../test/fixtures/sprites/README.md) |
| A large authored project: RmlUi documents, behaviors, virtual lists | Studio's own `projects/studio` in [chibi-studio](https://github.com/waldnercharles/chibi-studio); `projects/starter` is what a new project starts from |
| One UI widget by itself, with a log of its actions | Studio's Canvas: open a ui asset ([UI authoring](studio-ui-authoring.md)) |

Start a project with `new_project` ([runtime authoring](runtime-authoring.md#start-a-project)).
Publish a game's browser player and `/dev/` editor by copying
[`game-web-workflow.yml`](game-web-workflow.yml) to its `.github/workflows/web.yml`.

No checked-in project saves a declarative model; see
[declarative SDF modeling](declarative-sdf-modeling.md) for the syntax.

## Retired repositories

On 2026-10-17 these repositories were retired and archived read-only on GitHub:

- `chibi-examples`: Celeste, Stillwater Tetris, `hrc_lab`, `sdf_lab`,
  `modeling_lab`, `local_mesh_sampled` and `suzanne_sampled`. Its unpublished
  Terrain Lab moved here as `test/fixtures/terrain-lab`.
- `chibi-celeste` and `chibi-tetris`, the standalone games that `chibi-examples`
  had absorbed.
- `chibi-space-cadet`, `chibi-arena3d` and `chibi-voidweaver`.

Plans, research notes and evidence that name these projects describe work done
in them. No engine check reads them, and they are not migrated. The checks that
needed them were removed: `--examples-projects`, `--tilemap-project`, the five
`--installed-example-*` labs and the three installed SpaceCadet routes.
