"""Pause the real desktop loop for Codex, then validate and consume its reply."""

from __future__ import annotations

import hashlib
import json
import uuid
from dataclasses import asdict, replace

from typesafe_sdk import ChoiceAnswer

from . import macos
from .decide import Decision, kind_criteria, site_criteria
from .models import Abort


class Pause(Exception):
    def __init__(self, packet):
        self.packet = packet


class Finished(Exception):
    def __init__(self, answer):
        self.answer = answer


def signature(screen, items):
    # Compare semantic state and geometry. Blinking cursors do not affect OCR items.
    field = screen.field.record() if screen.field else None
    data = {
        "app": screen.app,
        "pid": screen.pid,
        "url": screen.url,
        "window": screen.window,
        "size": screen.image.size,
        "field": field,
        "items": [asdict(item) for item in items if not (item.y2 < 40 * screen.scale and item.x1 > screen.image.width * 0.6)],
        "offscreen": [(n.role, n.label, n.x, n.y, n.w, n.h) for n in screen.offscreen],
    }
    # OCR confidence can vary without the target changing.
    for item in data["items"]:
        item.pop("ocr_confidence")
    return hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()


def serialize_decision(decision):
    return {
        key: None
        if (value := getattr(decision, key)) is None
        else {"choice": value.choice, "confidence": value.confidence, "probabilities": value.probabilities}
        for key in ("kind", "item", "site", "offscreen")
    }


def restore_decision(record):
    return Decision(**{key: ChoiceAnswer(**value) if value else None for key, value in record.items()})


def snapshot(screen, items):
    return {
        "app": screen.app,
        "pid": screen.pid,
        "url": screen.url,
        "window": screen.window,
        "size": screen.image.size,
        "field": screen.field.record() if screen.field else None,
        "items": [asdict(item) for item in items],
        "offscreen": [(n.role, n.label, n.x, n.y, n.w, n.h) for n in screen.offscreen],
    }


def matching_items(before, screen, items, target=None):
    """Match labels and centers, allowing four points of OCR box jitter and sparse missed glyphs."""
    current = snapshot(screen, items)
    for key in ("app", "pid", "url", "window", "size", "field", "offscreen"):
        # Normalize tuples from memory to lists from checkpoints; compare numeric values, not their JSON spelling.
        if json.loads(json.dumps(before.get(key))) != json.loads(json.dumps(current[key])):
            return None
    mapping = {}
    used = set()
    for old in before["items"]:
        x, y = (old["x1"] + old["x2"]) / 2, (old["y1"] + old["y2"]) / 2
        matches = [
            it
            for it in items
            if it.index not in used
            and it.text == old["text"]
            and it.role == old["role"]
            and abs(it.center[0] - x) <= 4 * screen.scale
            and abs(it.center[1] - y) <= 4 * screen.scale
        ]
        if len(matches) == 1:
            mapping[str(old["index"])] = str(matches[0].index)
            used.add(matches[0].index)
    old_items, new_items = before["items"], items
    if target:
        # A click is bound to its control and surrounding menu, not unrelated disabled
        # labels in another panel that Vision may intermittently omit.
        x, y = (target["x1"] + target["x2"]) / 2, (target["y1"] + target["y2"]) / 2
        radius = 80 * screen.scale
        old_items = [
            it for it in old_items if abs((it["x1"] + it["x2"]) / 2 - x) < radius and abs((it["y1"] + it["y2"]) / 2 - y) < radius
        ]
        new_items = [it for it in new_items if abs(it.center[0] - x) < radius and abs(it.center[1] - y) < radius]
    matched = sum(str(it["index"]) in mapping for it in old_items)
    if matched < 0.9 * max(len(old_items), len(new_items)):
        return None
    return mapping


def click_target(record, items):
    if not record or record.get("kind") != "click_item":
        return None
    return next((it for it in items if str(it["index"]) == str(record.get("item"))), None)


def remap_decision(decision, mapping):
    if decision is None or not decision.clicking:
        return decision
    if decision.item.choice not in mapping:
        return None
    item = ChoiceAnswer(
        choice=mapping[decision.item.choice],
        confidence=decision.item.confidence,
        probabilities={mapping[k]: p for k, p in decision.item.probabilities.items() if k in mapping},
    )
    return replace(decision, item=item)


class Supervisor:
    def __init__(self, pending=None, reply=None, target_app=None):
        self.pending = pending
        self.reply = reply
        self.target_app = target_app
        self.view = None
        self.decision = None
        self.reviewed = False
        self.verify_text = False

    def bind(self, screen, items):
        self.view = screen, items
        if self.target_app and screen.app != self.target_app:
            self.ask("app_changed", {"expected_app": self.target_app})

    def ask(self, reason, extra=None):
        screen, items = self.view
        packet = {
            "request_id": uuid.uuid4().hex,
            "reason": reason,
            "signature": signature(screen, items),
            **snapshot(screen, items),
            "decision": serialize_decision(self.decision) if self.decision else None,
            **(extra or {}),
        }
        raise Pause(packet)

    def next_decision(self, screen, items):
        self.bind(screen, items)
        self.decision = None
        if self.verify_text:
            self.verify_text = False
            self.ask("verify_text", {"message": "Check the screenshot, then retry to continue."})
        if not self.pending:
            return None
        pending = self.pending
        self.pending = None
        reply = self.reply or {}
        if reply.get("retry"):
            # A retry authorizes a fresh model decision; it carries no old action or text.
            self.reply = None
            return None
        self.decision = restore_decision(pending["decision"]) if pending["decision"] else None
        proposed = reply.get("decision") or (
            {"kind": "click_item", "item": self.decision.item.choice} if self.decision and self.decision.clicking else None
        )
        mapping = matching_items(pending, screen, items, click_target(proposed, pending["items"]))
        remapped = remap_decision(self.decision, mapping) if mapping is not None else None
        if mapping is None or (self.decision and self.decision.clicking and remapped is None):
            self.reply = None
            self.decision = None
            self.ask("stale_observation", {"previous_request_id": pending["request_id"]})
        self.decision = remapped
        if "answer" in reply:
            if pending["reason"] != "verify_done":
                raise ValueError("A final answer requires a verify_done checkpoint")
            raise Finished(self.answer())
        if "decision" in reply:
            override = reply["decision"].copy()
            if override.get("kind") == "click_item":
                if str(override.get("item")) not in mapping:
                    self.reply = None
                    self.ask("stale_observation")
                override["item"] = mapping[str(override["item"])]
            self.decision = self.override(override, screen, items)
            self.reviewed = True
        elif reply.get("approve"):
            if not self.decision:
                raise ValueError("No proposed decision to approve")
            self.reviewed = True
        elif "text" not in reply and "url" not in reply and "answer" not in reply:
            raise ValueError("Reply requires approve, retry, decision, text, url, or answer")
        else:
            expected = {"text": "type_text", "url": "use_browser"}
            for key, kind in expected.items():
                if key in reply and (not self.decision or self.decision.kind.choice != kind):
                    raise ValueError(f"{key} does not match the pending action")
            self.reviewed = True
        if self.decision is None:
            self.ask("review", {"message": "Choose an action from the observed controls or retry."})
        return self.decision

    def override(self, record, screen, items):
        kind = record.get("kind")
        if kind not in kind_criteria("browser", None, bool(screen.offscreen)):
            raise ValueError("Unknown action kind")

        def answer(value):
            return ChoiceAnswer(choice=value, confidence=1.0, probabilities={value: 1.0})

        item = str(record.get("item", ""))
        offscreen = str(record.get("offscreen", ""))
        site = record.get("site", "none")
        if kind == "click_item" and item not in {str(it.index) for it in items}:
            raise ValueError("Click target is not in the observed items")
        if kind == "press_offscreen" and offscreen not in {str(i) for i in range(len(screen.offscreen))}:
            raise ValueError("Offscreen target is not observed")
        if site not in site_criteria():
            raise ValueError("Unknown site; choose other to request a URL")
        return Decision(
            kind=answer(kind),
            item=answer(item) if kind == "click_item" else None,
            site=answer(site),
            offscreen=answer(offscreen) if kind == "press_offscreen" else None,
        )

    def before_action(self, cfg, decision):
        self.decision = decision
        if not self.reviewed and (decision.stops or decision.confidence < cfg.min_confidence):
            self.ask("verify_done" if decision.kind.choice == "done" else "review")
        if self.target_app and decision.kind.choice == "use_browser":
            self.ask("review", {"message": "Browser switching is disabled for an app-scoped task."})
        reviewed = self.reviewed
        self.reviewed = False
        return reviewed

    def text(self):
        reply = self.reply or {}
        screen, _ = self.view
        if "text" not in reply:
            self.ask("text", {"canvas_field": not (screen.field and screen.field.is_text)})
        value = reply["text"]
        if not isinstance(value, str) or not value or len(value) > 4096:
            raise ValueError("Text must contain 1-4096 characters")
        if not (screen.field and screen.field.is_text) and reply.get("confirm_canvas_field") is not True:
            raise ValueError("An OCR-only field needs confirm_canvas_field after reviewing the screenshot")
        if screen.field and "Secure" in screen.field.role:
            raise ValueError("Secure fields cannot be filled")
        self.reply = None
        return value

    def url(self):
        reply = self.reply or {}
        if "url" not in reply:
            self.ask("url")
        from urllib.parse import urlsplit

        value = reply["url"]
        if not isinstance(value, str):
            raise ValueError("URL must be text")
        parsed = urlsplit(value)
        if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError("URL must use HTTP(S), without embedded credentials")
        if any(ch.isspace() for ch in value):
            raise ValueError("URL must not contain whitespace")
        self.reply = None
        return value

    def answer(self):
        reply = self.reply or {}
        if not isinstance(reply.get("answer"), str) or type(reply.get("achieved")) is not bool:
            self.ask("verify_done")
        self.reply = None
        return reply

    def check_focus(self):
        screen, _ = self.view
        if macos.frontmost_app_and_pid() != (screen.app, screen.pid):
            raise Abort("frontmost app changed before input")
        if macos.frontmost_window_bounds(screen.pid) != screen.window:
            raise Abort("frontmost window moved before input")

    def reobserve(self, goal):
        from .config import MAX_OPTIONS
        from .perception import capture, perceive

        self.check_focus()
        before, before_items = self.view
        screen = capture()
        items = perceive(screen, MAX_OPTIONS, goal)
        self.bind(screen, items)
        previous = snapshot(before, before_items)
        proposed = {"kind": "click_item", "item": self.decision.item.choice} if self.decision and self.decision.clicking else None
        mapping = matching_items(previous, screen, items, click_target(proposed, previous["items"]))
        decision = remap_decision(self.decision, mapping) if mapping is not None else None
        if mapping is None or (self.decision and self.decision.clicking and decision is None):
            self.decision = None
            self.ask("stale_observation", {"message": "The desktop changed while Laya was deciding. Retry from this capture."})
        self.decision = decision
        return screen, items, decision
