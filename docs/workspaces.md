# Shared workspaces for Codex and Claude

`chibi-workspace` allocates task checkouts for both desktop apps. Each task gets a
unique path and `cwaldner/` branch. Repeating its name returns the same checkout;
another task never inherits that path. The default limit is four unfinished tasks
per repository. Completing a task frees a slot while retaining its checkout,
branch, staged changes, untracked files and ignored files.

All checkouts use the existing shared dependency packages, FetchContent cache and
ccache. Cache reuse does not depend on the checkout's parent directory. Existing
Codex and Claude worktrees stay where they are and are outside this manager.

## Installation

```sh
python3 tools/install_workspace_tools.py --apply
~/.local/bin/chibi-workspace init --repo "$PWD" --limit 4
```

The installer copies the manager into `~/.local/share/chibi-workspaces/bin`, creates
`~/.local/bin/chibi-workspace`, merges hooks into both apps' user settings, and
installs an hourly macOS LaunchAgent. Existing settings are preserved and changed
files are backed up under `~/.local/share/chibi-workspaces/backups`. Run without
`--apply` to preview installation. Repeating installation updates the copied tools
without duplicating hooks. Use `--no-launch-agent` to omit the scheduled job.

Codex requires review of the new definitions in `/hooks`. The installer does not
change hook trust. Claude may need a new session to load settings. Hooks apply only
to registered managed checkouts. The installed CLI must support `codex app PATH`
and `claude --desktop`; the latter requires Claude Code 2.1.285 or newer.

## Start a task

From any directory, supply the source repository explicitly:

```sh
~/.local/bin/chibi-workspace acquire lighting-fix \
  --repo /Users/cwaldner/Personal/c-blueprints --app codex --open

~/.local/bin/chibi-workspace acquire editor-test \
  --repo /Users/cwaldner/Personal/c-blueprints --app claude --open
```

New branches start at the specified checkout's current `HEAD`. Uncommitted edits
are not copied. The launcher prints the allocated path and opens it as a local
project. Keep native worktree isolation off for this folder. Native worktree
buttons still use each app's own allocator; Codex has no documented hook that
replaces that allocation. Without `--open`, the command prints the path for manual
folder selection or CLI use. `list` prints task paths, branches and owners.

The launcher reserves a checkout for the selected app. A trusted session hook
claims it. Conflicting sessions receive a hook denial; ending a turn does not
release ownership. Closing a session marks it idle when the app emits SessionEnd.
A missed end hook conservatively leaves the reservation in place.

## Switch apps or finish

Stop the original session and its running commands before handing the folder to
the other app. Then release the recorded owner and reopen the same task:

```sh
~/.local/bin/chibi-workspace release /absolute/allocated/path
~/.local/bin/chibi-workspace acquire lighting-fix \
  --repo /Users/cwaldner/Personal/c-blueprints --app claude --open
```

Release revokes the previous session ID so a late end hook cannot release the new
owner, and the old session cannot silently reclaim the folder. It refuses while
the workspace build lock is held. It does not interrupt arbitrary running tools;
stop them before handoff. This is coordination through cooperative hooks, not an
OS sandbox for editors or shell commands launched outside the manager.

```sh
~/.local/bin/chibi-workspace release /absolute/allocated/path
~/.local/bin/chibi-workspace finish /absolute/allocated/path
~/.local/bin/chibi-workspace reopen /absolute/allocated/path
```

`finish` requires no recorded session/reservation or active build. It retains all
files and the branch. `reopen` makes that task available again, subject to the
pool limit. No checkout or branch is automatically deleted.

## Build retirement

In a managed checkout, ordinary `make` takes one OS lock for the entire invocation,
including recursive Make, tests and launched runtimes. Child processes inherit
the lock. Configuration registers only build directories that do not yet exist.
After a successful invocation, the manager records regular compiled/generated
Ninja outputs with their file identity and SHA-256 digest.

The hourly job keeps the three most recently used managed workspace build sets.
Older sets become eligible after seven idle days, with no owner, app reservation,
held lock or detected process activity. Retirement removes only unchanged,
verified outputs. Configuration, dependency sources, shared caches, tracked files,
unknown files, symlinks, edited outputs and directories containing repositories
are preserved. If an identity or activity check fails, that build is skipped.
Subsequent builds recreate removed outputs.

Existing build directories are never adopted by name or by `.gitignore`. They
remain outside automatic retirement. Use the manager for new tasks to accumulate
the evidence required for cleanup. Direct `cmake`/`ninja` invocations bypass Make's
lock; run them through `chibi-workspace run -- COMMAND` from the managed checkout.
Independent editors and processes must respect the same ownership convention.

Preview the policy without deleting files:

```sh
python3 ~/.local/share/chibi-workspaces/bin/workspace_retirement.py sweep
```

The command accepts `--keep` and `--idle-days`; deletion requires `--apply`.
The installed job uses `--keep 3 --idle-days 7 --apply`. Its plist is
`~/Library/LaunchAgents/com.chibi.workspace-retirement.plist`; its local logs are
under `~/.local/share/chibi-workspaces/logs`. It makes no model calls.

To stop scheduled cleanup:

```sh
launchctl bootout gui/$(id -u)/com.chibi.workspace-retirement
```

For isolated testing, set `CHIBI_WORKSPACE_HOME` before invoking the Python tools
directly. The installed launcher fixes the state location selected at install.

## App integration references

- [Codex hooks and required review](https://learn.chatgpt.com/docs/hooks)
- [Claude Desktop launch and shared configuration](https://code.claude.com/docs/en/desktop)
- [Claude hook contracts](https://code.claude.com/docs/en/hooks)
