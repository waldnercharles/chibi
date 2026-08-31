#!/usr/bin/env python3
"""Deterministically dispatch repair agents for actionable GitHub PR events."""
from __future__ import annotations

import fcntl
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = "waldnercharles/c-blueprints"
ROOT = Path(__file__).resolve().parents[2]
LOCK = Path.home() / ".cache" / "c-blueprints-pr-coordinator.lock"
PAUSE_FILE = ROOT / ".paseo" / "REPAIR_AGENTS_PAUSED"
STATE_FILE = Path.home() / ".cache" / "c-blueprints-pr-coordinator-state.json"
MODEL = os.environ.get("PASEO_REPAIR_MODEL", "openai-codex/gpt-5.6-luna")
PROVIDER = os.environ.get("PASEO_REPAIR_PROVIDER", "pi")


def run(*args: str) -> str:
    result = subprocess.run(args, check=True, text=True, capture_output=True)
    return result.stdout


def gh_json(*args: str):
    return json.loads(run("gh", *args, "--repo", REPO))


def login(item: dict) -> str:
    author = item.get("author") or item.get("user") or {}
    return str(author.get("login", "")).lower()


def copilot(item: dict) -> bool:
    name = login(item)
    return "copilot" in name


def review_sha(review: dict) -> str | None:
    commit = review.get("commit")
    if isinstance(commit, dict):
        return commit.get("oid")
    return review.get("commit_id") or review.get("commitment")


def active_repair_agents() -> list[dict]:
    """Return active repair agents; fail closed if Paseo cannot be queried."""
    try:
        agents = json.loads(run("paseo", "agent", "ls", "--json"))
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return [{"status": "unknown", "name": "Paseo status unavailable"}]
    return [
        agent for agent in agents
        if agent.get("status") in {"running", "starting", "waiting"}
        and re.search(r"(?:repair|implement|review)\s+pr\s*#?\s*\d+", agent.get("name", ""), re.I)
    ]


def active_repair_agent(number: str) -> bool:
    pattern = re.compile(rf"(?:pr\s*#?\s*{re.escape(number)}\b|pr-{re.escape(number)}(?:\b|-))", re.I)
    return any(
        pattern.search(f"{agent.get('name', '')} {agent.get('cwd', '')}")
        for agent in active_repair_agents()
    )


def main() -> int:
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    with LOCK.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if PAUSE_FILE.exists():
            print(f"Repair dispatch paused by {PAUSE_FILE}")
            return 0
        prs = gh_json("pr", "list", "--state", "open", "--json", "number,body,headRefName")
        try:
            state = json.loads(STATE_FILE.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            state = {}
        active = active_repair_agents()
        if active and active[0].get("status") == "unknown":
            print("Paseo status unavailable; refusing to dispatch")
            return 1
        # Serialize repair work globally so switching to a local-only machine
        # cannot accidentally create parallel implementation agents.
        if active:
            print(f"Repair agent already active ({active[0].get('name')}); skipping dispatch")
            return 0

        # Initial issue implementation uses this same serialized dispatcher; the
        # separate event-driven Paseo workflow must not create a second worker.
        issues = json.loads(run("gh", "issue", "list", "--state", "open",
                                "--label", "ready-for-agent", "--json", "number,title,url", "--repo", REPO))
        for issue in sorted(issues, key=lambda item: item["number"]):
            number = str(issue["number"])
            issue_key = f"issue:{number}:implementation"
            if issue_key in state:
                continue
            issue_ref = re.compile(rf"(?:issues/|(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s*#)" + re.escape(number) + r"\b", re.I)
            if any(
                issue_ref.search(f"{p.get('body', '')} {p.get('headRefName', '')}")
                for p in prs
            ):
                continue
            state[issue_key] = {"dispatched": True}
            STATE_FILE.write_text(json.dumps(state, indent=2) + "\n")
            prompt = f"""Implement issue {issue['url']} in the repository.

Read the complete issue, repository instructions, and applicable skills. Use /implement,
/tdd, and /code-review as applicable. Implement only this issue, verify its acceptance
criteria, run focused and required local checks, create a descriptive agent branch, and
open a pull request linked to the issue. Do not merge. Do not poll remote CI. Do not post
pickup or completion comments; report the result only through the agent session."""
            result = subprocess.run(
                ["paseo", "agent", "run", prompt, "--background", "--cwd", str(ROOT),
                 "--provider", PROVIDER, "--model", MODEL, "--new-workspace", "worktree",
                 "--worktree-mode", "branch-off", "--new-branch", f"agent/issue-{number}",
                 "--base", "origin/main", "--title", f"Implement issue #{number}",
                 "--label", f"issue={number}"], check=True, text=True, capture_output=True,
            )
            print(json.dumps({"issue": number, "reason": "implementation", "agent": result.stdout.strip()}))
            return 0

        for listed in sorted(prs, key=lambda p: p["number"]):
            pr = gh_json(
                "pr", "view", str(listed["number"]), "--json",
                "number,headRefOid,url,mergeStateStatus,statusCheckRollup,reviews,comments,closingIssuesReferences",
            )
            number = str(pr["number"])
            head = pr["headRefOid"]
            comments = pr.get("comments") or []
            # `gh pr view --json comments` is not reliable across gh versions;
            # fetch issue comments directly because dispatch markers live there.
            issue_comments = gh_json_from_api(f"repos/{REPO}/issues/{number}/comments")
            inline = gh_json_from_api(f"repos/{REPO}/pulls/{number}/comments")
            all_comments = comments + issue_comments + inline
            bodies = [str(c.get("body", "")) for c in all_comments]
            if active_repair_agent(number):
                print(f"PR #{number}: repair agent already active; skipping")
                continue

            conflict = str(pr.get("mergeStateStatus", "")).upper() == "DIRTY"
            requested_changes = any(
                review_sha(r) == head and str(r.get("state", "")).upper() == "CHANGES_REQUESTED"
                for r in (pr.get("reviews") or [])
            )
            failed = any(
                str(check.get("conclusion", "")).upper() in {"FAILURE", "TIMED_OUT", "CANCELLED"}
                or str(check.get("state", "")).upper() in {"FAILURE", "ERROR"}
                for check in (pr.get("statusCheckRollup") or [])
            )

            if conflict:
                reason = "merge conflict"
            elif requested_changes:
                reason = "local review findings"
            elif failed:
                reason = "failed required/current-head CI check"
            elif f"{number}:{head}:local code review" not in state:
                reason = "local code review"
            else:
                continue
            event_key = f"{number}:{head}:{reason}"
            if event_key in state:
                continue

            issue_refs = pr.get("closingIssuesReferences") or []
            issue = issue_refs[0].get("number") if issue_refs else None
            issue_text = f" Issue: https://github.com/{REPO}/issues/{issue}." if issue else ""
            # Keep orchestration silent on GitHub. State is local and comments are
            # deliberately avoided so repair agents do not add pickup/completion noise.
            state[event_key] = {"dispatched": True}
            STATE_FILE.write_text(json.dumps(state, indent=2) + "\n")
            if reason == "local code review":
                prompt = f"""Review PR {pr['url']} at current head {head}.

Read and follow `.github/skills/code-review/SKILL.md`, the originating issue acceptance criteria,
and repository instructions. Review the complete diff and relevant surrounding code. Run
focused local checks when practical, but do not poll or wait for remote CI. Report only
specific, actionable findings as an inline GitHub pull-request review. Use CHANGES_REQUESTED
when actionable findings exist. If the acceptance criteria are fully met, the changes are
safe, and the authenticated GitHub identity is allowed to approve this PR, approve it with
GitHub; otherwise do not approve. Never bypass GitHub's self-approval or permission rules.
Do not edit files or push commits. If there are no actionable findings, do not post a summary
comment. Do not post pickup or completion comments."""
            else:
                prompt = f"""Repair PR {pr['url']} at current head {head}.

Reason for dispatch: {reason}.{issue_text}
Read the originating issue acceptance criteria, repository instructions, current Copilot
findings/inline threads, merge-conflict details if applicable, and the failed-check details if applicable. Use /implement,
/tdd, and /code-review as applicable. Fix actionable findings, resolve merge conflicts against the base branch when applicable, add or update tests, and
push only the PR branch. Do not poll, wait for, or repeatedly query remote CI; run local
checks and stop. Resolve only review threads whose findings you directly addressed via
GitHub GraphQL resolveReviewThread; leave disputed or unfixed threads open. If you push changes, stop after local verification; the deterministic coordinator will
run the local code-review agent on the new head. Do not approve or merge. Do not post
pickup or completion comments; report the result only through the agent session."""
            result = subprocess.run(
                ["paseo", "agent", "run", prompt, "--background", "--cwd", str(ROOT),
                 "--provider", PROVIDER, "--model", MODEL, "--new-workspace", "worktree",
                 "--worktree-mode", "checkout-pr", "--pr-number", number, "--forge", "github",
                 "--title", f"{'Review' if reason == 'local code review' else 'Repair'} PR #{number} ({reason})", "--label", f"pr={number}",
                 "--label", f"head={head}"],
                check=True, text=True, capture_output=True,
            )
            print(json.dumps({"pr": number, "head": head, "reason": reason, "agent": result.stdout.strip()}))
            return 0
        print("No PR requires dispatch")
        return 0


def gh_json_from_api(endpoint: str):
    return json.loads(run("gh", "api", endpoint, "--paginate", "--slurp"))[0]


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(exc.stderr or str(exc), file=sys.stderr)
        raise
    except BlockingIOError:
        print("Another coordinator run is already active")
