#!/usr/bin/env python3
"""Mint a short-lived GitHub App installation token without changing gh auth."""
from __future__ import annotations

import base64
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

APP_ID = os.environ.get("GITHUB_APP_ID", "4776555")
INSTALLATION_ID = os.environ.get("GITHUB_INSTALLATION_ID", "157862366")
KEY = Path(os.environ.get("GITHUB_APP_PRIVATE_KEY", str(Path.home() / ".config/github/cute-engine.pem")))


def b64(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def main() -> None:
    now = int(time.time())
    header = b64(json.dumps({"alg": "RS256", "typ": "JWT"}, separators=(",", ":")).encode())
    payload = b64(json.dumps({"iat": now - 60, "exp": now + 540, "iss": APP_ID}, separators=(",", ":")).encode())
    unsigned = f"{header}.{payload}".encode()
    signature = subprocess.check_output(
        ["openssl", "dgst", "-sha256", "-sign", str(KEY)], input=unsigned
    )
    jwt = f"{header}.{payload}.{b64(signature)}"
    request = urllib.request.Request(
        f"https://api.github.com/app/installations/{INSTALLATION_ID}/access_tokens",
        data=json.dumps({
            "repositories": ["c-blueprints"],
            "permissions": {
                "contents": "write",
                "issues": "write",
                "metadata": "read",
                "pull_requests": "write",
            },
        }).encode(),
        method="POST",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {jwt}",
            "X-GitHub-Api-Version": "2022-11-28",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(request) as response:
        result = json.load(response)
    print(result["token"])


if __name__ == "__main__":
    main()
