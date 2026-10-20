# Examples

Chibi has no separate example projects. Studio and these documents teach the
engine. Each project that you can learn from is checked in beside the code
that it uses. Thus each project migrates with the engine, and the engine tests
it.

To open one of the projects in Studio, run this command:

```sh
make studio-run STUDIO_SOURCE=/absolute/path/to/chibi-studio PROJECT_ROOT=test/fixtures/feature-coverage
```

| To see | Open |
| --- | --- |
| Each reflected component, native function and event in a running scene | `test/fixtures/feature-coverage`. It has one scene for each area, for example Audio, Entities, Flow, Input, Materials, Particles, Physics2D, Physics3D, Rendering3D, Rhythm, Save, Tilemap and Ui. Its `coverage-gaps.txt` lists the items that no scene uses yet, with the reasons. |
| A small game that you can play: movement, live terrain cuts, save and load, and a HUD | `test/fixtures/terrain-lab` |
| 3D rooms and shapes | `test/fixtures/scene3d` |
| 2.5D HRC lighting | `test/fixtures/courtyard` |
| Sprite frames | `test/fixtures/sprites` |
| A large authored project: RmlUi documents, behaviors and virtual lists | The `projects/studio` project of Studio, in [chibi-studio](https://github.com/waldnercharles/chibi-studio). A new project starts from `projects/starter`. |
| One UI widget alone, with a log of its actions | The Canvas of Studio. Open a ui asset. |

To start a project, use the `new_project` operation of the controller. It
writes `project.kdl` and an empty start scene.

To publish the browser player and the `/dev/` editor of a game, use the game web
workflow. Copy it into the game repository as `.github/workflows/web.yml`.

No checked-in project saves a declarative model yet.

## Retired repositories

On 2026-10-17, the separate example and game repositories were retired and
archived. Their Terrain Lab moved to this repository as
`test/fixtures/terrain-lab`. Nothing else from them was migrated. No engine
check reads them.
