# Installed example acceptance

The `--installed-example-<lab>` selectors in `test/tools` run one pinned start
scene from each external `chibi-examples` lab. They require real installed
compiler and player packages. They do not load project sources in the player.

Each selector verifies both package inventories with the production validators,
compares their exact pinned package IDs and shared runtime contracts, then exports
the reviewed Git tree for its lab. The compiler can migrate this disposable copy.
The test checks the prepared artifact fingerprint and every external resource's
length and fingerprint, copies only that resource closure, then deletes the source
copy. It relocates the artifact, resources and standalone player and runs from a
scratch directory outside the engine source and build directories.

The player draws four hidden frames at a fixed 1/60-second step and saves the last
320-by-180 frame. PNG decoding checks dimensions, opacity and broad scene landmarks. The sampled
box also checks missing-texture compiler rejection and invalid capture options.
Its texture is embedded, so deployed playback needs no copy of that source PNG.

## Reproduce a pinned run

Build the native driver with `make test-build`. Build the compiler package with
`make package-sdk` and the standalone player with `make package-player`; keep their
build directories and installation prefixes separate. Configuration, compilation
and packaging are outside the test deadline. `chibi-examples/acceptance/README.md`
records the exact source revision and package pins for its recorded run.

Set these absolute paths:

- `CHIBI_EXAMPLES_ROOT`: a Git checkout containing the pinned example tree objects.
- `CHIBI_EXAMPLES_SDK`: installed compiler SDK.
- `CHIBI_EXAMPLES_PLAYER`: installed standalone player package.
- `CHIBI_EXAMPLES_PIN`: JSON with `compiler_build_id`, `player_build_id`, and a
  `projects` object mapping lab names to reviewed Git tree IDs.
- `CHIBI_EXAMPLES_EVIDENCE`: an existing empty evidence directory.

Then run one selector, with the native driver's build directory:

```sh
make agent-gpu-test BUILD_DIR=build AGENT_SELECTOR=--installed-example-local_mesh_sampled
```

The other admitted names are `hrc_lab`, `modeling_lab`, `sdf_lab` and
`suzanne_sampled`. Each command has its own shared 60-second supervisor deadline;
there is no aggregate examples suite. Package verification is capped at 15 seconds,
compilation at 20, and player startup, four frames, capture and shutdown at 25.
The box's missing-input compile is capped at 10 seconds. All phases consume the
same deadline. Installed inventories and sources have explicit count/byte bounds.
Changing reviewed input trees requires a corresponding review of admission.

Each run leaves compiler/player logs, the resource manifest, `result.txt` and
`frame.png` under the lab's evidence directory. Completed test cases remove generated packages, project copies and prepared
artifacts. A process abort or outer timeout may leave its named scratch directory
for diagnosis.
Use a fresh evidence parent when repeating a lab.

## Player capture

```sh
chibi-player game.chibip --resource-root . --hidden --frames 4 \
  --fixed-dt 0.016666666666666666 --size 320x180 --capture frame.png
```

`--size` fixes both canvas dimensions independently of display DPI, with a maximum
side of 640 and 230,400 total pixels. Capture requires an explicit size and finite
frame count, fails on early exit, and returns failure if readback or PNG output
fails. Readback has a ten-second deadline. Native capture is separate from Studio
and the controller preview path.

This acceptance covers the pinned default scene, resource closure and one bounded
visual output. It does not establish temporal convergence, alternate scenes,
interactive controls, performance, browser behavior or full rendering-suite
coverage. A package or source build alone does not count as visual acceptance.
