# Studio study

An interactive study of the RmlUi Studio workspace, using Neon Swarm's Sandbox scene, prototypes and behaviors. See the [design notes](DESIGN.md) and the [component kit](kit.html).

```sh
python3 docs/prototypes/studio/serve.py 8140
```

Open http://127.0.0.1:8140/docs/prototypes/studio/ (workspace) or `kit.html` (components). The server disables caching so edits appear on reload. `fixture.js` is a snapshot of `projects/arena2d` content from before assets moved to KDL; it does not track later changes.

Everything stays in memory and resets on reload; nothing writes project files. The Solid view draws entities from their authored transforms and appearance. Rendered shows a frame captured from the engine, aligned to world space. Playback moves a few objects to exercise live, read-only values; it does not run the engine. Output lines are samples.

This directory is outside the production build.
