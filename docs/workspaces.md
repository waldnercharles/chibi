# Storage management with the normal desktop workflow

Use the normal worktree checkbox or selector when starting a chat in Codex or
Claude Desktop for c-blueprints.
The installed global hooks register the actual checkout used by each session.
No launcher, pool slot, manual finish, or separate Claude CLI login is required.
The apps continue to choose paths and branches and perform their normal archival.
Codex's detached worktrees remain detached.

New worktrees based on updated `main` or `origin/main` include the build tools.
An older starting branch may lack the integration. Global hooks still observe
those sessions, but automatic retirement stays disabled until the checkout
includes the build tools. The apps create and own their worktrees normally;
the hooks register the resulting checkout without replacing worktree creation.

SessionStart, UserPromptSubmit and PreToolUse discover the current Git root. This
also covers resuming a chat, changing folders and handing a chat between Local
and Worktree. Hooks only enroll repositories registered with this installation.
Local sessions in the primary checkout use the same activity tracking.

The hooks track each app/session independently. A stopped turn becomes idle;
ending a session clears its activity across any checkouts it used. One chat's
Stop event cannot mark a different chat idle. No manual ownership handoff is
needed for app-created checkouts. A build or background process still protects
its files even after its chat stops.

## Shared caches and builds

Dependency packages, FetchContent and ccache remain shared across checkout
locations and both apps. A new checkout does not need its own compiled copy of
the shared dependencies.

Ordinary `make` holds one inherited OS lock across configuration, recursive Make,
compilation, tests and launched runtimes. The configure wrapper registers a build
directory only when it is first created. After a successful invocation, the
manager records generated Ninja outputs with their identity and SHA-256 digest.

The existing hourly job retains the three most recently used workspace build
sets. Older sets become eligible after seven idle days, with no active session,
held build lock, detected process activity or reference from another checkout's
CMake cache. It removes only unchanged verified outputs. Configuration, source,
unknown files, tracked files, symlinks and shared caches are preserved. A later
build recreates the removed outputs. The job makes no model calls.

Existing build folders are not automatically adopted. An ignored directory or a
CMakeCache.txt file alone does not prove its contents are disposable. Older source
branches also need the Make/configure integration before their new builds can
participate. Sessions are still observed when that integration is absent, but
retirement is disabled for those entries.

For direct `cmake` or `ninja` use, run the command through
`~/.local/bin/chibi-workspace run -- COMMAND` from the checkout. Normal Make builds
already do this. Arbitrary tools outside the hooks/build wrapper are not governed
by the cooperative lock; process inspection provides an additional conservative
check.

## Installation and app trust

The installation on this computer already registers c-blueprints. To install on
another machine or update the copied helper scripts:

```sh
python3 tools/install_workspace_tools.py --apply
~/.local/bin/chibi-workspace init --repo "$PWD"
```

The installer preserves existing settings and backs up changed files under
`~/.local/share/chibi-workspaces/backups`. It installs helper scripts there and
merges five global hooks into `~/.codex/hooks.json` and
`~/.claude/settings.json`. Running the installer without `--apply` previews
changes. Hook commands use the installed copy, so archiving this development
checkout does not break them.

Codex requires reviewing new hook definitions in `/hooks` before they run.
Claude applies its normal workspace trust rules. Installation does not bypass
these app controls or prove that the app has loaded the hooks. New sessions load
the configured hooks; an already-running session may need to be resumed or
restarted according to the app's settings-loading behavior.

The earlier custom allocator remains an optional CLI facility. Its four-task
limit and explicit release/finish commands apply only to checkouts created with
`acquire`, not to the built-in desktop workflow.

## Inspection and cleanup policy

```sh
~/.local/bin/chibi-workspace list
python3 ~/.local/share/chibi-workspaces/bin/workspace_retirement.py sweep
```

`sweep` previews candidates by default. Deletion requires `--apply`; `--keep`
and `--idle-days` change its policy. The installed job uses
`--keep 3 --idle-days 7 --apply`. Its plist is
`~/Library/LaunchAgents/com.chibi.workspace-retirement.plist`, and logs are under
`~/.local/share/chibi-workspaces/logs`.

## Idle builds and finished worktrees

The same hourly job also runs `tools/worktree_reaper.py` for every registered
repository, including builds that predate the registry and worktrees outside it:

- A top-level `build*` directory whose `.ninja_log` is two days old and that no
  process has open has its Ninja outputs removed with `ninja -t clean`. Sources,
  configuration, scratch files and shared caches are untouched, and the next build
  recreates the outputs from ccache.
- A worktree is removed with `git worktree remove` only when it is clean, its HEAD
  is on a remote branch, its HEAD reflog and builds are three days old, it is not
  locked, and no process uses it. Anything with uncommitted or unpushed work is
  reported and kept. Ignored files are not uncommitted work and go with the checkout.

```sh
python3 tools/worktree_reaper.py            # preview every registered repository
python3 tools/worktree_reaper.py --apply    # clean and remove now
```

Rerun `python3 tools/install_workspace_tools.py --apply` once to install the
updated helper scripts.

To stop scheduled cleanup:

```sh
launchctl bootout gui/$(id -u)/com.chibi.workspace-retirement
```

For isolated testing, set `CHIBI_WORKSPACE_HOME` when invoking the Python tools
directly. The installed helper command fixes the installation's state directory.

- [Codex hook lifecycle and review](https://learn.chatgpt.com/docs/hooks)
- [Claude Desktop shared configuration](https://code.claude.com/docs/en/desktop#shared-configuration)
- [Claude hook contracts](https://code.claude.com/docs/en/hooks)
