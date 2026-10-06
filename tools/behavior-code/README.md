# Behavior code bridge

This source front end edits the same typed behavior graphs used by the visual editor and SLJIT runtime. Its current authorable slice includes reflected component aliases and event payloads, persistent typed arrays and sets, array `for` iteration, and set membership/insertion/removal. Read the [language contract](LANGUAGE.md) before writing source.

```c
behavior ProjectilePulse {
  state int[] marks = [1, 2, 1];
  state set<int> seen = [];

  on Tick(GameProjectile projectile) {
    for (mark in marks) {
      if (seen.contains(mark)) {
        projectile.age += event.dt;
      } else {
        seen.add(mark);
      }
    }
  }
}
```

## Commands

```sh
node tools/behavior-code/cli.mjs validate tools/behavior-code/examples/ProjectilePulse.code --project-root . --ctl build/chibi_ctl
node tools/behavior-code/cli.mjs compile tools/behavior-code/examples/ProjectilePulse.code --project-root . --ctl build/chibi_ctl
node tools/behavior-code/cli.mjs decompile tools/behavior-code/examples/ProjectilePulse.code --project-root . --ctl build/chibi_ctl
```

`validate` is read-only and prints JSON diagnostics with stable codes and source spans. It checks the reflected names and supported syntax. `compile` then creates or updates the ordinary KDL graph asset and asks the native graph compiler to validate it before publication. `decompile` rewrites supported graph expression spans into code and refuses unsupported mapped wiring. Source-owned nodes retain sidecar identities across safe statement edits; explicit `// @node` names are only needed to resolve ambiguous identity matches.

The existing `content/behaviors/ProjectileLifecycle.code` uses named component aliases and `event.dt`. Its graph was recompiled with the native controller while preserving all five mapped node IDs; code-to-graph-to-code comparison is byte-for-byte identical.

Small source examples live in `tools/behavior-code/examples/`: `ProjectilePulse.code` exercises typed array iteration and set operations, while `ContactRegistry.code` records event entity handles in a `set<entity>`. Both compile into ordinary behavior graph assets. The `ProjectilePulseProbe` prototype runs the generated collection graph against a native projectile component.

State variable shape and default values are fixed after the first source compile in this slice. The bridge rejects source edits to those declarations rather than silently replacing persistent graph state. The visual editor can still edit the generated graph. Sets are not iterable; set methods accept a same-typed array-loop element or direct event payload field. `entity` sets start empty and can store entity handles from payloads such as `event.other`.

For now, `.code` is authoring input, `.map.json` retains stable graph identities,
and the existing `.kdl` behavior graph remains the project asset. The intended
direction is complete KDL replacement with canonical `.code` and direct native
graph construction. Behavior graphs are the first milestone; the broader goal
includes every KDL-backed asset domain. This prototype has not performed that
migration. See the [source roadmap](../../docs/code-source-roadmap.md) and CUT-70;
CUT-71 defines the next shared C frontend implementation.

## Checks

The script-based parser and controller smoke tests are retired. They are absent
from `make test-tools`; the source authoring utility remains available through
its commands above.

Historical validation accepted all three behavior sources. The earlier full
`make test` gate passed in Debug with address and undefined-behavior sanitizers;
the native suite took 476.4 seconds and the whole-function audit took 2.3 seconds,
with no reported sanitizer findings. These results describe that revision and
its former test coverage.

Four native simulation steps at `1/60` second produce `GameProjectile.age = 0.16666667` in the collection-loop probe, with one live entity and no behavior failures. The lifecycle probe removes its projectile by frame six with no behavior failures, exercising deferred destruction. These are functional results, not timing claims.

`validate` discovers component and event schemas through the engine's `describeComponents` and `searchNodes` APIs. Parser and resolver diagnostics carry stable codes and source spans; native graph-build failures are returned with the owning statement span. An LSP can remain a thin future adapter over the same validator. A source-versus-graph runtime benchmark would compare the same compiled graph because this bridge does not add a separate interpreter, so no speed claim is made.
