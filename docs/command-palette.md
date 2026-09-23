# Editor command palette

Press Cmd+K (Ctrl+K on other platforms) to enter a command. The native palette
uses Laya by default. The settings button selects a provider and host; the
native Laya API key can be entered there for the current editor session. It can
also be supplied through `LAYA_API_KEY` or `LAYA_API_KEY_FILE` (default
`.chibi/laya-api-key`). Authenticated native requests require a loopback host,
such as a local tunnel to the Laya service.

The editor's command path is C: it observes the current catalog, narrows the
target and property, prepares bounded Laya choices when needed, validates a
typed proposal, then presents the exact edit for review. No Python resolver is
launched. The Laya model is still a separate, resident service.

Current direct edits include prototype appearance colors, collider shapes, and
particle start sizes. A named scene instance resolves to its prototype unless
the request says to change only that instance. For example, “Change Ship's
color to blue” edits the Player prototype when Ship uses Player. “Change the
player's hitbox to a circle” edits the reflected collider choice. “Increase
the size of the particles” scales both start axes by 25%; “by 50%” uses the
given percentage. The review shows the resulting values before Apply.

Unresolved or low-confidence targets produce a short clarification menu. Commands
that require graph construction, new assets, or multi-step planning are not yet
covered by this Laya path. Choose another configured provider in settings for
those commands. The palette does not claim a task succeeded until the native
executor applies its reviewed proposal.

`build/test_suite --command-model` runs the command contract and in-memory
editor flow without a window. The full headless checks run with `make test`.
