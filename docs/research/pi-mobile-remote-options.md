# Android apps for remotely driving Pi Coding Agent

Research date: 2026-08-31

## Short answer

For this use case, the strongest candidates are:

1. **PI WEB (`jmfederico/pi-web`)** — best overall fit if a browser/PWA is acceptable. It is Pi-native, substantially more mature than the other Pi-specific Android options, keeps sessions alive, supports multiple sessions/workspaces/worktrees, and has explicit support for Pi extensions and extension dialogs.
2. **Pico (`anthaathi/Pico`)** — best Pi-specific native Android experiment. It has an Android APK, a server/agent model, workspaces and sessions, and a useful current audience signal (136 GitHub stars), but its server is more opinionated and its extension/subagent compatibility is less clearly documented.
3. **Shellular** — most interesting polished cross-agent phone app. It explicitly supports Pi, has Android and Google Play distribution, and provides files, diffs, terminals, localhost access, and E2E encryption. However, the open-source host repository currently has only 3 commits/60 stars, so it does not meet the requested maturity preference yet.
4. **SSH + tmux via TermRover or Termux** — safest long-term fallback. It runs the actual Pi process unchanged, so every Pi extension, subagent package, and todo/task workflow remains available. The tradeoff is a terminal UX rather than an agent-aware mobile UI.

## Comparison

| Option | Android | Pi compatibility | Sessions / agents | Extensions and tasks | Maturity / risk |
|---|---|---|---|---|---|
| [PI WEB](https://github.com/jmfederico/pi-web) | Browser/PWA; installable from browser | First-class Pi runtime | Persistent sessions, projects, workspaces, worktrees, multiple sessions, remote machines | Strongest evidence: Pi packages, extension initialization, `ctx.ui` dialogs, notifications, and tracked/spawned subsessions are supported | 580 stars, 1,120 commits; MIT. Still requires trusted networking and is not a sandbox |
| [Pico](https://github.com/anthaathi/Pico) | Android APK from releases; web/desktop too | Pi-specific server manages Pi | Workspaces and sessions; QR pairing; background service | Pi runs on the server, but README does not document preservation of arbitrary Pi extension UI or subagent tracks; verify before adopting | 136 stars; Apache-2.0. Promising, but much newer and more opinionated than PI WEB |
| [Shellular](https://github.com/shellular-org/shellular) | Android app / Google Play | Explicitly lists Pi among supported agents | Multiple agents, terminal, editor, files/diffs, localhost and DevTools | Likely preserves the CLI workflow, but public docs do not establish Pi-specific subagent/task integration | 60 stars, 3 commits in host repo; AGPL-3.0. Product looks more mature than its public repo signal |
| [π Remote](https://github.com/nucleoid/pi-remote) | Signed APK via GitHub Releases; no Play/F-Droid | Pi extension; controls existing Pi TUI or RPC session | Monitors/steers an existing visible session; session picker | Most direct TUI control, but it is a thin remote for an existing process, not a headless workspace/session manager | 0 stars; early public release. Clear security limitations; trusted LAN/VPN/tunnel only |
| [Remote Pi](https://github.com/jacobaraujo7/remote_pi) | Google Play, App Store, APK | Pi-specific extension/app | QR-paired chat with local agent | Baseline/reference project; check its current extension behavior against your task package needs | User's reference; compare before switching |
| [TermRover](https://termrover.sh/) | Android and iOS apps; Android currently closed beta per feedback repo | Agent-agnostic SSH/tmux; Pi works unchanged | Persistent remote tmux sessions; image attachment and voice input | Full Pi extension/subagent/task support because it only transports the real terminal; no semantic task UI | Closed-source, very new (2 GitHub stars in feedback repo); promising UX but not mature |
| [Termux](https://github.com/termux/termux-app) + tmux | Android; F-Droid/GitHub canonical builds, separate Play build | Agent-agnostic; run Pi over SSH or locally | tmux persistence | Maximum compatibility; no purpose-built mobile agent UI | Most established/open fallback, but Android can kill long-running processes; configure battery/process exclusions |
| [Happy](https://github.com/slopus/happy) | Android, iOS, web | Claude Code and Codex, not Pi | Strong remote sessions, notifications, voice, E2E encryption | Not a Pi option without a substantial adapter | 23.6k stars, 1,120 commits; mature, but wrong agent |
| [Cordless](https://github.com/naveenneog/cordless) | Android APK/PWA | README documents shell, Claude and Codex; not Pi | Persistent PTYs and multiple sessions | Agent-agnostic terminal model could run Pi manually, but no documented Pi integration/task UI | Very new; good transport idea, not yet a Pi recommendation |

## Relevance to subagents and todo/task extensions

The key distinction is whether the mobile app **runs Pi itself through a compatible RPC/session layer** or merely displays/controls a terminal.

- **Best bet: PI WEB.** Its documentation explicitly says it runs the installed Pi CLI, keeps sessions in real workspaces, initializes Pi extensions for web-managed sessions, supports Pi extension dialogs (`confirm`, `select`, `input`), and has tracked/spawned subsessions. This is the only candidate found with strong public evidence for preserving the Pi extension surface that task/todo packages depend on.
- **Safest compatibility: SSH/tmux.** Pi, its installed packages, native subagents, and task/todo extensions see the same terminal and filesystem as usual. You give up mobile-native rendering and semantic controls.
- **Pico and π Remote** are Pi-native, but public documentation is not sufficiently explicit about arbitrary extension UI, subagent lifecycle, or task/todo rendering. They deserve hands-on tests rather than a confident compatibility claim.
- **Shellular** is attractive if you want a polished cross-agent app, but its Pi support is advertised at a high level and the public implementation is very young. Test extension dialogs, tool calls, child sessions, images, and long-running sessions before relying on it.

## Recommended evaluation order

1. **Try PI WEB first.** Use the official `jmfederico/pi-web` package, keep the server behind Tailscale/SSH/authenticated reverse proxy, and test the exact extensions that currently disappoint you in Paseo. Its browser/PWA model works well on Android without an APK.
2. **Keep Termux + tmux as the compatibility baseline.** If any web UI loses an extension capability, this is the fallback that cannot reinterpret Pi's protocol.
3. **Try Pico next** if you prefer a native Android client and want a Pi-focused project with more public adoption than π Remote.
4. **Try Shellular** if files/diffs, localhost previews, and switching among multiple agent CLIs matter more than Pi-specific orchestration.
5. **Use π Remote** only if controlling an already-running visible Pi TUI is exactly the desired workflow and sideloading an APK plus VPN-only networking is acceptable.

## Security notes

Do not expose a raw Pi/WebSocket control port to the public internet. Prefer Tailscale, WireGuard, or an SSH tunnel. PI WEB explicitly assumes trusted users/repositories and is not a sandbox. π Remote explicitly warns against public exposure and intentionally uses cleartext `ws://` behind a trusted network/tunnel. Shellular's relay is E2E encrypted, but the phone, host, and paired credentials remain security-critical.

## Sources

- PI WEB README: https://github.com/jmfederico/pi-web
- PI WEB release notes (extension dialogs, extension initialization, tracked subsessions): https://github.com/jmfederico/pi-web/releases
- PI WEB site: https://pi-web.dev/
- Pico README: https://github.com/anthaathi/Pico
- Shellular README: https://github.com/shellular-org/shellular
- Shellular site: https://shellular.dev/
- π Remote README/security notes: https://github.com/nucleoid/pi-remote
- Remote Pi README: https://github.com/jacobaraujo7/remote_pi
- TermRover site: https://termrover.sh/
- TermRover feedback repository: https://github.com/ptgamr/termrover-feedback
- Termux README: https://github.com/termux/termux-app
- Tailscale Android installation: https://tailscale.com/docs/install/android
- Happy README: https://github.com/slopus/happy
- Cordless README: https://github.com/naveenneog/cordless
