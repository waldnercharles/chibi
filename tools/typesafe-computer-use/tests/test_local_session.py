"""Exercise the upstream loop across separate supervised invocations."""

import json
from dataclasses import replace
from types import SimpleNamespace

import httpx2 as httpx
import pytest
from typesafe_sdk import Choice, ChoiceAnswer

from typesafe_computer_use import macos, perception, runner, session
from typesafe_computer_use.decide import Decision
from typesafe_computer_use.laya import LayaClient
from typesafe_computer_use.models import Field
from typesafe_computer_use.runner import RunConfig
from typesafe_computer_use.supervisor import Supervisor, matching_items, remap_decision, snapshot


def decision(kind, item=None, confidence=0.95):
    def answer(key):
        return ChoiceAnswer(choice=key, confidence=confidence, probabilities={key: 1.0})

    return Decision(answer(kind), answer(item) if item else None, answer("none"))


@pytest.fixture
def desktop(screen, make_item, monkeypatch, tmp_path):
    field = Field("AXTextField", "Asset name", "", "", 30, 50, 150, 20, ref="name")
    state = SimpleNamespace(stage="start", text="", clicks=[], calls=0, moved=False)

    def capture(*_, **__):
        current = replace(field, value=state.text) if state.stage == "name" else None
        return replace(screen, app="Lab", pid=42, field=current)

    def perceive(*_, **__):
        labels = {"start": "New asset", "name": "Save", "saved": "Asset saved"}
        return [
            make_item(0, labels[state.stage], x1=700 if state.moved else 100, x2=1000 if state.moved else 400, y1=150, y2=180)
        ]

    def click(point):
        state.clicks.append(point)
        state.stage = "name" if state.stage == "start" else "saved"

    def choose(*_):
        state.calls += 1
        if state.stage == "start":
            return decision("click_item", "0")
        if state.stage == "name":
            return decision("click_item", "0") if state.text else decision("type_text")
        return decision("done")

    monkeypatch.setattr(session, "ROOT", tmp_path / "locks")
    monkeypatch.setattr(macos, "accessibility_trusted", lambda: True)
    monkeypatch.setattr(macos, "focus_process", lambda _: True)
    monkeypatch.setattr(macos, "check_abort", lambda: None)
    monkeypatch.setattr(macos, "sleep_watching", lambda _: None)
    monkeypatch.setattr(macos, "frontmost_app_and_pid", lambda: ("Lab", 42))
    monkeypatch.setattr(macos, "frontmost_window_bounds", lambda _: None)
    monkeypatch.setattr(macos, "focused_field", lambda: capture().field)
    monkeypatch.setattr(macos, "ax_focus", lambda _: None)
    monkeypatch.setattr(macos, "ax_set_value", lambda ref, text: setattr(state, "text", text) or True)
    monkeypatch.setattr(macos, "ax_value", lambda _: state.text)
    monkeypatch.setattr(macos, "click_at", click)
    monkeypatch.setattr(macos, "type_text", lambda _: pytest.fail("AX field should receive text directly"))
    monkeypatch.setattr(runner, "capture", capture)
    monkeypatch.setattr(runner, "perceive", perceive)
    monkeypatch.setattr(perception, "capture", capture)
    monkeypatch.setattr(perception, "perceive", perceive)
    monkeypatch.setattr(runner, "decide", choose)
    return state


def start(tmp_path, **kwargs):
    return session.start(RunConfig(goal="Create an asset", out=tmp_path / "run", act=True, target_app="Lab", delay=0, **kwargs))


def reply(result, **values):
    return session.resume(result["run_dir"], result["pending"]["request_id"], values)


def test_click_text_resume_save_and_verify(tmp_path, desktop):
    first = start(tmp_path)
    assert first["pending"]["reason"] == "text"
    assert len(desktop.clicks) == 1 and desktop.text == ""
    completed_actions = reply(first, text="Laya Marker")
    assert desktop.text == "Laya Marker" and len(desktop.clicks) == 2
    assert completed_actions["pending"]["reason"] == "verify_done"
    assert completed_actions["goal_achieved"] is None
    verified = reply(completed_actions, answer="Laya Marker saved", achieved=True)
    assert verified["outcome"] == "complete" and verified["goal_achieved"] is True
    assert verified["steps_taken"] == 3 and desktop.calls == 4
    with pytest.raises(ValueError, match="already consumed"):
        reply(first, text="duplicate")
    assert desktop.text == "Laya Marker"


def test_stale_observation_does_not_type(tmp_path, desktop):
    first = start(tmp_path)
    desktop.moved = True
    stale = reply(first, text="wrong location")
    assert stale["pending"]["reason"] == "stale_observation"
    assert desktop.text == "" and len(desktop.clicks) == 1
    retried = reply(stale, retry=True)
    assert retried["pending"]["reason"] == "text"


def test_action_budget_survives_resumes(tmp_path, desktop):
    first = start(tmp_path, steps=1, burst=1)
    assert len(desktop.clicks) == 1
    limited = reply(first, retry=True)
    assert limited["pending"]["reason"] == "verify_done"
    final = reply(limited, answer="Only opened the name field", achieved=False)
    assert final["outcome"] == "incomplete" and len(desktop.clicks) == 1


def test_dry_run_never_performs_or_requests_text(tmp_path, desktop):
    result = session.start(RunConfig("Create an asset", tmp_path / "dry", target_app="Lab"))
    assert result["outcome"] == "dry run" and not result["pending"]
    assert desktop.clicks == []


def test_replay_cannot_be_promoted_to_live_input(tmp_path, desktop):
    result = session.start(RunConfig("Create an asset", tmp_path / "replay", act=True, image=tmp_path / "saved.png"))
    assert result["outcome"] == "dry run" and desktop.clicks == []


def test_low_confidence_requires_review(tmp_path, desktop, monkeypatch):
    monkeypatch.setattr(runner, "decide", lambda *_: decision("click_item", "0", confidence=0.01))
    result = start(tmp_path, burst=1)
    assert result["pending"]["reason"] == "review" and desktop.clicks == []
    approved = reply(result, approve=True)
    assert len(desktop.clicks) == 1 and approved["pending"]["reason"] == "continue"


def test_wrong_app_stops_before_deciding(tmp_path, desktop):
    result = session.start(RunConfig("Create an asset", tmp_path / "wrong", act=True, target_app="Another app"))
    assert result["pending"]["reason"] == "app_changed"
    assert desktop.calls == 0 and not desktop.clicks


def test_reply_consumed_before_failed_input(tmp_path, desktop, monkeypatch):
    first = start(tmp_path)

    def fail(*_):
        raise RuntimeError("input interrupted")

    monkeypatch.setattr(macos, "ax_set_value", fail)
    with pytest.raises(RuntimeError, match="input interrupted"):
        reply(first, text="Laya Marker")
    assert session.status(first["run_dir"])["outcome"] == "failed"
    with pytest.raises(ValueError, match="already consumed"):
        reply(first, text="Laya Marker")


def test_laya_maps_choices_and_logs_only_local_requests(tmp_path, monkeypatch):
    monkeypatch.setenv("LAYA_API_KEY", "test-secret")
    requests = []

    def respond(request):
        packet = json.loads(request.content)
        requests.append(packet)
        keys = list(packet["questions"]["choice"]["criteria"])
        return httpx.Response(
            200,
            json={
                "answers": {
                    "choice": {
                        "choice": keys[-1],
                        "confidence": 0.8,
                        "probabilities": {key: float(key == keys[-1]) for key in keys},
                    }
                }
            },
        )

    trace = tmp_path / "trace.jsonl"
    with LayaClient(trace, transport=httpx.MockTransport(respond)) as client:
        answer = client.system_one(
            state={"goal": "Select item 11"},
            questions={"next": Choice(instructions="Pick", criteria={f"item-{i}": f"Item {i}" for i in range(12)})},
        ).answers["next"]
    assert answer.choice == "item-11" and answer.confidence == 0.8
    assert len(requests) == 2 and all(len(p["questions"]["choice"]["criteria"]) <= 6 for p in requests)
    assert "test-secret" not in trace.read_text()


@pytest.mark.parametrize(
    "answer",
    [
        {"choice": "outside", "probabilities": {"0": 0.5, "1": 0.5}, "confidence": 1},
        {"choice": "0", "probabilities": {"0": 0.1, "1": 0.9}, "confidence": 0.5},
        {"choice": "0", "probabilities": {"0": 0.5, "1": 0.5}, "confidence": 5},
    ],
)
def test_invalid_provider_answers_are_never_decisions(answer):
    transport = httpx.MockTransport(lambda _: httpx.Response(200, json={"answers": {"choice": answer}}))
    with LayaClient(transport=transport) as client, pytest.raises(ValueError):
        client.choose({}, Choice(instructions="Choose", criteria={"a": "A", "b": "B"}))


def test_ocr_jitter_remaps_the_same_control_after_a_missed_label(screen, make_item):
    old = [make_item(i, f"Control {i}", x1=100 + i * 100, x2=150 + i * 100) for i in range(20)]
    new = [replace(item, index=i, x1=item.x1 + 0.00003, x2=item.x2 + 3) for i, item in enumerate(old[1:])]
    mapping = matching_items(snapshot(screen, old), screen, new)
    assert mapping["10"] == "9"
    assert remap_decision(decision("click_item", "10"), mapping).item.choice == "9"
    assert remap_decision(decision("click_item", "0"), mapping) is None


def test_changed_field_or_moved_control_invalidates_reply(screen, make_item):
    item = make_item(0, "Save")
    before = snapshot(screen, [item])
    assert matching_items(before, screen, [replace(item, x1=400, x2=700)]) is None
    field = Field("AXTextField", "Name", "", "unexpected value", 1, 1, 10, 10)
    assert matching_items(before, replace(screen, field=field), [item]) is None


def test_canvas_typing_requires_explicit_visual_confirmation(screen):
    supervisor = Supervisor(reply={"text": "hello"})
    supervisor.bind(screen, [])
    with pytest.raises(ValueError, match="confirm_canvas_field"):
        supervisor.text()
    supervisor.reply = {"text": "hello", "confirm_canvas_field": True}
    assert supervisor.text() == "hello"


def test_interrupted_worker_is_not_reported_running(tmp_path, desktop):
    first = start(tmp_path)
    path = tmp_path / "run/session.json"
    stored = json.loads(path.read_text())
    stored["state"].update(outcome="running", pending=None)
    path.write_text(json.dumps(stored))
    assert session.status(first["run_dir"])["outcome"] == "interrupted"
    with pytest.raises(ValueError, match="already consumed"):
        reply(first, text="must not replay")


def test_canvas_menu_labels_remain_individually_clickable(screen, monkeypatch):
    lines = [("Material", 1.0, (100, 100, 200, 120)), ("Shape", 1.0, (100, 124, 200, 144))]
    monkeypatch.setattr(perception, "ocr_lines", lambda *_: (lines, 100, 0))
    items = perception.ocr(screen, 255, "Create an asset")
    assert [it.text for it in items] == ["Material", "Shape"]
    assert items[0].center[1] < items[1].y1


def test_background_text_cannot_change_foreground_target_ids(screen, make_item, monkeypatch):
    scoped = replace(screen, window=(100, 100, 400, 300))
    background = make_item(0, "Other app", x1=0, y1=40, x2=80, y2=80)
    target = make_item(1, "New", x1=250, y1=250, x2=350, y2=280)
    monkeypatch.setattr(perception, "ocr", lambda *_: [background, target])
    monkeypatch.setattr(perception, "ax_nodes", lambda *_: ([], []))
    items = perception.perceive(scoped, 255, "Create an asset")
    assert [(it.index, it.text) for it in items] == [(0, "New")]
