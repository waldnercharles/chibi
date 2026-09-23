# Chibi MCP adapter

This package contains the model-facing stdio adapter and its shared public
tool catalog. The native `chibi_ctl` controller provides headless automation
and an explicit `--render` mode for GPU previews.

From a checked-out repository, run:

```sh
make mcp-setup
```

The generated Codex configuration points at `launch.sh`. The launcher locates
the repository from its own path, builds the native controllers when needed,
writes diagnostics to stderr, and reserves stdout for MCP JSON-RPC.

See the repository's [MCP workflow](https://github.com/waldnercharles/c-blueprints/blob/main/docs/mcp.md)
for authoring and validation guidance.

## Laya decision-path experiment

`node tools/mcp/laya-cli.mjs request.json` accepts either a flat `actions` menu
or a `tree` of focused questions. For a tree, include `available_action_ids`
from the current read-only action catalog. For example:

```json
{
  "goal": "Change the player's hitbox to a circle",
  "observation": "Player is the prototype of the selected Ship instance.",
  "available_action_ids": ["player_hitbox", "player_color"],
  "tree": {
    "question": "Where should this change be made?",
    "children": [
      {
        "id": "prototype",
        "label": "Player prototype",
        "question": "Which property should change?",
        "children": [
          { "id": "player_hitbox", "label": "Hitbox shape" },
          { "id": "player_color", "label": "Sprite color" }
        ]
      },
      { "id": "scene_hitbox", "label": "Scene instance hitbox" }
    ]
  }
}
```

The search removes unavailable leaves before inference, makes at most three
Laya requests by default, and skips questions with only one remaining choice.
The caller must derive allowed leaves from current editor facts. In this
example, it excludes the Ship override because the field is inherited and the
editing policy is to change Player. The chooser does not infer that policy
from prose alone.
It uses the returned probabilities to try at most one other branch if a later
dry run rejects a leaf. A backtracked proposal requires supervisor review.
The result includes each question, score and elapsed time. The command only
proposes an action and path; it never edits the project. This is a standalone
chooser experiment, not an editor integration or an accuracy result. Run
`node --test tools/mcp/laya.test.mjs` for its headless contract tests. A live
Laya service is required to measure model quality and latency.

On one read-only Ship/Player probe (September 22), Laya selected the prototype
at the broad target question but paused at entropy confidence 0.065 (281 ms).
A more specific target question selected the instance, also with low confidence
(0.005, 292 ms). When the known inheritance policy removed instance edits,
Laya selected `GameAppearance.color` on Player at confidence 0.814 in one
314 ms request. This is a single diagnostic case, not an accuracy estimate or
an end-to-end editor latency measurement.
