"""Local Laya provider for the upstream typed-decision interface."""

from __future__ import annotations

import json
import math
import os
import time
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlsplit

import httpx2 as httpx
from typesafe_sdk import Choice, ChoiceAnswer

OPTIONS = 6


def compact_state(state: dict) -> dict:
    """Keep the goal and current field ahead of history within the English checkpoint's budget."""
    field = state.get("focused_field") or {}
    return {
        "goal": str(state.get("goal", ""))[:200],
        "app": str(state.get("frontmost_app", ""))[:60],
        "field": {k: str(field[k])[:80] for k in ("role", "label", "placeholder", "current_value") if k in field},
        "visible": " | ".join(str(it["text"])[:40] for it in state.get("screen_items_in_reading_order", [])[:12])[:280],
        "last_action": str((state.get("previous_actions") or [""])[-1])[:100],
    }


class LayaClient:
    def __init__(self, trace: Path | None = None, transport=None):
        self.url = os.environ.get("LAYA_URL", "http://127.0.0.1:8768/v1/decide")
        parsed = urlsplit(self.url)
        if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError("LAYA_URL must be an HTTP endpoint without embedded credentials")
        key = os.environ.get("LAYA_API_KEY", "")
        key_path = Path(os.environ.get("LAYA_API_KEY_FILE", Path(__file__).resolve().parents[3] / ".chibi/laya-api-key"))
        if not key and key_path.is_file():
            key = key_path.read_text().strip()
        self.http = httpx.Client(
            timeout=15, follow_redirects=False, transport=transport, headers={"Authorization": f"Bearer {key}"} if key else {}
        )
        self.trace = trace

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.http.close()

    def _ask(self, state, instructions, criteria):
        keys = list(criteria)
        packet = {
            "state": state,
            "questions": {
                "choice": {
                    "type": "choice",
                    "instructions": instructions[:140],
                    "criteria": {str(i): str(criteria[key])[:88] for i, key in enumerate(keys)},
                }
            },
        }
        started = time.monotonic()
        response = self.http.post(self.url, json=packet)
        response.raise_for_status()
        data = response.json()
        answer = data.get("answers", {}).get("choice", {})
        probabilities = answer.get("probabilities", {})
        expected = set(packet["questions"]["choice"]["criteria"])
        if set(probabilities) != expected or answer.get("choice") not in expected:
            raise ValueError("Laya returned an unknown choice or incomplete probabilities")
        if any(type(p) not in (int, float) or not math.isfinite(p) or not 0 <= p <= 1 for p in probabilities.values()):
            raise ValueError("Laya returned invalid probabilities")
        confidence = answer.get("confidence")
        if type(confidence) not in (int, float) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
            raise ValueError("Laya returned invalid confidence")
        if abs(sum(probabilities.values()) - 1) > 0.01 or probabilities[answer["choice"]] + 1e-6 < max(probabilities.values()):
            raise ValueError("Laya choice disagrees with its distribution")
        if self.trace:
            with self.trace.open("a") as out:
                out.write(json.dumps({"request": packet, "response": data, "seconds": time.monotonic() - started}) + "\n")
        return ChoiceAnswer(
            choice=keys[int(answer["choice"])],
            confidence=confidence,
            probabilities={keys[int(k)]: p for k, p in probabilities.items()},
        )

    def choose(self, state, question):
        criteria = dict(question.criteria)
        if not criteria:
            raise ValueError("A choice needs options")
        if len(criteria) == 1:
            key = next(iter(criteria))
            return ChoiceAnswer(choice=key, confidence=1.0, probabilities={key: 1.0})
        confidence = 1.0
        while len(criteria) > OPTIONS:
            keys = list(criteria)
            width = math.ceil(len(keys) / OPTIONS)
            groups = [keys[i : i + width] for i in range(0, len(keys), width)]
            group_labels = {
                str(i): " / ".join(str(criteria[k])[: max(4, 70 // len(group))] for k in group) for i, group in enumerate(groups)
            }
            group = self._ask(state, "Which group contains the best next target or action for the goal?", group_labels)
            confidence = min(confidence, group.confidence)
            criteria = {k: criteria[k] for k in groups[int(group.choice)]}
        answer = self._ask(state, str(question.instructions), criteria)
        # Probabilities describe the final group. Each grouping decision is recorded separately.
        return ChoiceAnswer(
            choice=answer.choice, confidence=min(confidence, answer.confidence), probabilities=answer.probabilities
        )

    def system_one(self, *, state, questions):
        answers = {}
        for key, question in questions.items():
            if not isinstance(question, Choice):
                raise ValueError("The local action policy accepts Choice questions; field verification uses readback")
            answers[key] = self.choose(compact_state(state), question)
        return SimpleNamespace(answers=answers)
