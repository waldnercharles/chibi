"""The TypeSafe side: state, criteria, and the one multi-Choice request."""

from __future__ import annotations

from dataclasses import dataclass

from typesafe_sdk import Choice, ChoiceAnswer, Noul, TypeSafeClient

from .config import SITES
from .dates import date_hints, now_context
from .models import AxNode, Field, Item, Screen

STOP_KINDS = ("done", "none")
OFFSCREEN_PREFIX = "offscreen:"
PRESS_OFFSCREEN = (
    "Activate a labelled control that the app exposes but that is not currently visible on screen "
    "(chosen in the offscreen question). Use when the needed control is known to exist but is "
    "scrolled out of view or not yet shown."
)


def fixed_actions(browser: str, email: str | None) -> dict[str, str]:
    """Deterministic actions offered alongside click_item. Keep them mutually exclusive."""
    actions = {
        "use_browser": (
            f"Work in {browser}: bring it to the front, and open a website there if one is needed. The "
            "site question says which website, or says that the page already open there is the one to "
            "continue with. This is the only way to reach a website: never click the address bar, a URL, "
            "or a search box to get there. Works from any app, including this one."
        ),
        "type_text": (
            "Type free text into the focused text field. A writing model composes the text from the "
            "goal and the field's label. Only valid when a text field is focused and needs content."
        ),
        "press_enter": "Press Return to submit the focused form or field.",
        "press_escape": "Press Escape to dismiss a dialog, menu, or popup.",
        "scroll_down": "Scroll down to reveal more of the page.",
        "scroll_up": "Scroll up.",
        "wait": "Nothing to do yet; the screen is still loading or changing.",
        "done": "The goal is already achieved.",
        "none": "Nothing on screen or in this list helps with the goal.",
    }
    if email:
        actions["type_email"] = (
            "Type the user's email address into the focused text field. Use this, not type_text, "
            "whenever the field wants an email or username."
        )
    return actions


def kind_criteria(browser: str, email: str | None, offscreen: bool = False) -> dict[str, str]:
    clicks = {"click_item": "Click one of the on-screen text items (chosen in the item question)."}
    if offscreen:
        clicks["press_offscreen"] = PRESS_OFFSCREEN
    return {**clicks, **fixed_actions(browser, email)}


def item_criteria(screen: Screen, items: list[Item]) -> dict[str, str]:
    """Each item as one line. A role prefix marks the ones the app itself declared."""
    hints = date_hints(items, screen)
    return {
        str(it.index): (
            f"{it.role + ' ' if it.from_ax and it.role else ''}{it.text!r} "
            f"({screen.region(it)}{'; ' + hints[it.index] if it.index in hints else ''})"
        )
        for it in items
    }


def offscreen_criteria(nodes: list[AxNode]) -> dict[str, str]:
    """Each off-screen control as one line, keyed by its position in `screen.offscreen`."""
    return {str(i): f"{node.role_word} {node.label!r} (not visible)" for i, node in enumerate(nodes)}


def offscreen_records(nodes: list[AxNode]) -> list[dict]:
    """The same controls as state, with the key the offscreen question answers with."""
    return [{"k": i, "role": node.role_word, "label": node.label} for i, node in enumerate(nodes)]


def site_criteria() -> dict[str, str]:
    """Which website use_browser opens. The catalog, plus one key for anything else and one for nothing."""
    return {
        **SITES,
        "other": "A website is needed to progress the goal, but it is not one of the sites named in this list.",
        "none": "No website needs to be opened: the page already open in the browser is the one to continue with.",
    }


def base_state(goal: str, screen: Screen, items: list[Item], history: list[str]) -> dict:
    hints = date_hints(items, screen)
    return {
        "goal": goal,
        "now": now_context(),
        "frontmost_app": screen.app,
        "browser_active_tab_url": screen.url,
        "focused_field": screen.field.summary() if screen.field else None,
        "previous_actions": history[-8:],
        "screen_items_in_reading_order": [
            {
                "i": it.index,
                "text": it.text,
                "where": screen.region(it),
                **({"role": it.role} if it.role else {}),
                **({"when": hints[it.index]} if it.index in hints else {}),
            }
            for it in items
        ],
        **({"offscreen_controls": offscreen_records(screen.offscreen)} if screen.offscreen else {}),
    }


@dataclass(frozen=True)
class Decision:
    kind: ChoiceAnswer
    item: ChoiceAnswer | None
    site: ChoiceAnswer
    offscreen: ChoiceAnswer | None = None

    @property
    def clicking(self) -> bool:
        return self.kind.choice == "click_item" and self.item is not None

    @property
    def pressing_offscreen(self) -> bool:
        return self.kind.choice == "press_offscreen" and self.offscreen is not None

    @property
    def chosen(self) -> str:
        if self.clicking:
            return self.item.choice
        if self.pressing_offscreen:
            return f"{OFFSCREEN_PREFIX}{self.offscreen.choice}"
        return self.kind.choice

    @property
    def confidence(self) -> float:
        # Only the answers that name a target lower the confidence: a click or a press lands
        # somewhere, and the wrong somewhere is not undone. use_browser reads the site answer too,
        # but every outcome of it is a page the next step can leave, so a split there must not
        # stop the run.
        if self.clicking:
            return min(self.kind.confidence, self.item.confidence)
        if self.pressing_offscreen:
            return min(self.kind.confidence, self.offscreen.confidence)
        return self.kind.confidence

    @property
    def stops(self) -> bool:
        return self.kind.choice in STOP_KINDS


def decide(
    client: TypeSafeClient, goal: str, screen: Screen, items: list[Item], history: list[str], browser: str, email: str | None
) -> Decision:
    state = base_state(goal, screen, items, history)
    kinds = {
        "click_item": "Click a visible control or text label",
        "type_text": "Ask Codex for text to type into the focused field",
        "press_enter": "Press Return to submit the current field",
        "press_escape": "Press Escape to dismiss a menu or dialog",
        "scroll_down": "Scroll down to reveal more controls",
        "scroll_up": "Scroll up to reveal earlier controls",
        "wait": "Wait for the screen to finish loading",
        "done": "Goal achieved; ask Codex to verify the result",
        "none": "No useful action; ask Codex for help",
    }
    if not items:
        kinds.pop("click_item")
    if screen.offscreen:
        kinds["press_offscreen"] = "Activate an offscreen accessibility control"
    if browser:
        kinds["use_browser"] = "Open or switch to the web browser"

    def choose(instructions, criteria):
        return client.system_one(state=state, questions={"choice": Choice(instructions=instructions, criteria=criteria)}).answers[
            "choice"
        ]

    kind = choose("Choose the next action for the goal from the current screen. Avoid repeating failed actions.", kinds)
    item = (
        choose("Which visible target advances the goal?", item_criteria(screen, items)) if kind.choice == "click_item" else None
    )
    offscreen = (
        choose("Which offscreen control advances the goal?", offscreen_criteria(screen.offscreen))
        if kind.choice == "press_offscreen"
        else None
    )
    site = (
        choose("Which site should the browser open?", site_criteria())
        if kind.choice == "use_browser"
        else ChoiceAnswer(choice="none", confidence=1.0, probabilities={"none": 1.0})
    )
    return Decision(kind=kind, item=item, site=site, offscreen=offscreen)


def verify_typed(client: TypeSafeClient, goal: str, field_before: Field, typed: str, field_after: Field | None) -> float:
    """Probability that the field now holds a sensible value for its purpose."""
    state = {
        "goal": goal,
        "field": field_before.summary(),
        "text_typed": typed,
        "field_value_now": field_after.value[:300] if field_after else None,
        "field_still_focused": bool(
            field_after and field_after.role == field_before.role and field_after.label == field_before.label
        ),
    }
    question = Noul(
        instructions=(
            "Did the typing succeed: does the field now contain the typed text, and is that "
            "text a sensible value for what this field asks for, given the goal?"
        )
    )
    return client.system_one(state=state, questions={"ok": question}).answers["ok"].noul
