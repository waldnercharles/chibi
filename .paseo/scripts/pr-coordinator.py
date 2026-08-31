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
MODEL = "openai-codex/gpt-5.6-luna"


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


def active_repair_agent(number: str) -> bool:
    """Avoid dispatching while a Paseo agent already owns this PR."""
    try:
        agents = json.loads(run("paseo", "agent", "ls", "--json"))
    except subprocess.CalledProcessError:
        # A failed status query is fail-closed: do not risk duplicate work.
        return True
    pattern = re.compile(rf"(?:pr\s*#?\s*{re.escape(number)}\b|pr-{re.escape(number)}(?:\b|-))", re.I)
    return any(
        agent.get("status") in {"running", "starting", "waiting"}
        and pattern.search(f"{agent.get('name', '')} {agent.get('cwd', '')}")
        for agent in agents
    )


def main() -> int:
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    with LOCK.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        prs = gh_json("pr", "list", "--state", "open", "--json", "number")
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

            review_event = any(
                copilot(c) and c.get("commit_id") == head
                for c in inline
            ) or any(
                copilot(r) and review_sha(r) == head
                for r in (pr.get("reviews") or [])
            )
            conflict = str(pr.get("mergeStateStatus", "")).upper() == "DIRTY"
            failed = any(
                str(check.get("conclusion", "")).upper() in {"FAILURE", "TIMED_OUT", "CANCELLED"}
                or str(check.get("state", "")).upper() in {"FAILURE", "ERROR"}
                for check in (pr.get("statusCheckRollup") or [])
            )

            if conflict and f"<!-- paseo-merge-conflict: {head} -->" not in bodies:
                reason = "merge conflict"
                marker = f"<!-- paseo-merge-conflict: {head} -->"
            elif review_event and f"<!-- paseo-copilot-review: {head} -->" not in bodies:
                reason = "current-head Copilot review feedback"
                marker = f"<!-- paseo-copilot-review: {head} -->"
            elif failed and f"<!-- paseo-ci-failure: {head} -->" not in bodies:
                reason = "failed required/current-head CI check"
                marker = f"<!-- paseo-ci-failure: {head} -->"
            else:
                continue

            issue_refs = pr.get("closingIssuesReferences") or []
            issue = issue_refs[0].get("number") if issue_refs else None
            issue_text = f" Issue: https://github.com/{REPO}/issues/{issue}." if issue else ""
            body = (
                f"{marker}\n\nPaseo is dispatching a repair agent for {reason}."
                f"{issue_text}"
            )
            run("gh", "pr", "comment", number, "--repo", REPO, "--body", body)
            prompt = f"""Repair PR {pr['url']} at current head {head}.

Reason for dispatch: {reason}.{issue_text}
Read the originating issue acceptance criteria, repository instructions, current Copilot
findings/inline threads, merge-conflict details if applicable, and the failed-check details if applicable. Use /implement,
/tdd, and /code-review as applicable. Fix actionable findings, resolve merge conflicts against the base branch when applicable, add or update tests, and
push only the PR branch. Do not poll, wait for, or repeatedly query remote CI; run local
checks and stop. Resolve only review threads whose findings you directly addressed via
GitHub GraphQL resolveReviewThread; leave disputed or unfixed threads open. If you push
changes, request a fresh Copilot review using requestReviewsByLogin with
botLogins=[copilot-pull-request-reviewer]. Do not approve or merge. Leave a concise PR
comment summarizing the work and local verification."""
            result = subprocess.run(
                ["paseo", "agent", "run", prompt, "--background", "--cwd", str(ROOT),
                 "--provider", "pi", "--model", MODEL, "--new-workspace", "worktree",
                 "--worktree-mode", "checkout-pr", "--pr-number", number, "--forge", "github",
                 "--title", f"Repair PR #{number} ({reason})", "--label", f"pr={number}",
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
