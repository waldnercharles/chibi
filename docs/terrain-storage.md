# Terrain storage

Terrain checkpoints preserve one authored `MaterialTerrain` source across native
player restarts. They contain cell occupancy, signed distances and accumulated
partial damage. They do not save actors, detached fragments, fluids, inventory,
quests or editor documents. This facility supplies runtime terrain storage; it
does not define layered environment authoring or an automatic save policy.

## Authored requests and completion

Use the source entity and a stable identity owned by the game:

```chibi
request := save_terrain(terrain, "terrain-lab-quarry-v1")
// load_terrain(terrain, "terrain-lab-quarry-v1") has the same request shape.
if request.Request == 0 {
    // The enqueue was rejected; display request.Error.
} else {
    // Retain request.Request and poll from a later Tick.
}
```

`terrain_storage_result(token)` returns `Pending`, `Succeeded` and `Error`.
A positive token means the request was accepted, not that the file was written or
loaded. While `Pending` is true, keep polling. When it becomes false, inspect
`Succeeded`; a failed completion supplies `Error`. There is one pending request
per world. A second pending request is rejected with token zero. A new accepted
request expires the previous result; unknown, expired and nonpositive tokens
report an error. Scene replacement cancels pending work and invalidates results.

The engine flushes accepted requests after authored phases finish and structural
commands publish. It prepares queued terrain edits before capturing a save.
Loading validates and stages a replacement before publishing its terrain, then
prepares resident tiles and derived geometry. It propagates their transforms
before rendering. Physics refreshes collision shapes at its next normal boundary,
before solving; completion does not mean an earlier authored raycast in the same
step already saw the restored shapes. Direct C hosts must synchronize physics
before inspecting shapes and propagate transforms before rendering.

`test/fixtures/terrain-lab/content/behaviors/Explorer.chibi` shows
request retention, polling, HUD errors and game-owned respawn after load. Its
Quarry scene uses identity `terrain-lab-quarry-v1`, explicit S/L controls and no
startup save/load. The example's restart procedure is described in its README;
these instructions do not claim that procedure has been validated on a device.

## Native storage namespace

The host must bind durable storage. The native player accepts:

```sh
chibi-player /absolute/path/to/game.chibip --save /absolute/path/to/quarry-save.json
```

Use the same namespace path and terrain identity after restart. A normal native
host can also choose its stable project-name save namespace; unbound in-memory
worlds cannot write binary attachments. The authored API accepts an identity of
1–64 bytes. Its attachment filename is:

```text
<save-path>.blob-<lowercase hexadecimal identity bytes>.bin
```

Binary attachments commit independently of text save entries. Calling
`commit_save` is not required for a terrain request and does not combine the two
into one transaction. The parent directory must already exist and be writable.
Browser binary attachments are unavailable, including WebGPU hosts; ordinary
browser text saves do not change that limitation.

A native write uses a same-directory temporary file, flushes and synchronizes its
contents, then renames it over the attachment. Failed writes preserve the prior
attachment. This supports explicit save/load across process restarts; it is not
a promise of recovery from power loss or a multi-file atomic save.

## Snapshot compatibility and failures

The portable version-1 codec is bounded to 256 tiles and 64 MiB per source.
Finite snapshots include empty chunks. Streamed snapshots combine visited cold
and resident tiles; untouched space still comes from the matching recipe.
The codec accepts identities up to 1024 bytes, but the authored/host attachment
API deliberately limits them to 64 bytes.

Identity and recipe must match. The recipe fingerprint includes the resolved
field program and dependencies, effective typed parameters, seed, grid/cell size,
cleanup settings, stream mode and chunk layout. The reader checks the version,
checksum, bounds, identity and recipe before replacing terrain. Derived contours,
collision shapes and neighbor rings are rebuilt, rather than serialized.

Missing files, inaccessible storage, unsupported browser storage, incompatible
recipes and invalid snapshots complete with an error. Rejected snapshot data
does not replace the current terrain; queued edits may already have been applied
at the preparation boundary. A load can publish valid cells and then fail while
preparing their residency or geometry. That completion reports "Terrain restored, but preparation failed"; it does not roll the imported cells back. A storage
failure is a retained request result, not an engine failure. Imported terrain rejects later
incompatible generation or
placement changes until the application explicitly resets the material runtime.
Reset discards current runtime terrain and cold storage; it does not delete the
host attachment. The game decides whether to regenerate or load again.
