"""Exercise structured handoff/readback without substituting model quality tests."""

import json
from types import SimpleNamespace

import pytest
from typesafe_sdk import ChoiceAnswer

from typesafe_computer_use import editor, session
from typesafe_computer_use.runner import RunConfig


@pytest.fixture
def app(tmp_path, monkeypatch):
    state = SimpleNamespace(stage="home", value="", revision=1, instance="test", calls=[], selected="")

    def item(identity, label, role="button", enabled=True, active=False):
        return {
            "id": identity,
            "label": label,
            "role": role,
            "value": state.value if role == "textbox" else "",
            "enabled": enabled,
            "active": active,
            "focused": active,
            "blocked": False,
        }

    def snapshot():
        items = {
            "home": [item("content.create", "New asset")],
            "menu": [item(key, "Material" if key == "content.new_material" else key) for key in editor.FOLDERS],
            "name": [
                item("scene.new.name", "Asset name", "textbox", active=True),
                item("scene.new.create", "Create Material", enabled=bool(state.value)),
            ],
            "document": [item("file.menu", "File")],
            "file": [item("file.save", "Save")],
            "saved": [],
        }[state.stage]
        return {
            "contract": 1,
            "ok": True,
            "pid": 1,
            "instance": state.instance,
            "revision": state.revision,
            "state": {
                "controls": items,
                "content_root": str(tmp_path),
                "popup_depth": 0,
                "document": state.value,
                "graph_dirty": state.stage == "document",
                "message": "",
            },
        }

    class Connection:
        def __init__(self, _):
            pass

        def request(self, operation, observation=None, **values):
            if operation == "snapshot":
                return snapshot()
            if observation["revision"] != state.revision:
                raise ValueError("Stale observation")
            identity = values["id"]
            state.calls.append((operation, identity))
            if identity == "content.create":
                state.stage = "menu"
            elif identity in editor.FOLDERS:
                state.stage = "name"
                state.selected = identity
            elif identity == "scene.new.name":
                state.value = values["text"]
            elif identity == "scene.new.create":
                state.stage = "document"
            elif identity == "file.menu":
                state.stage = "file"
            elif identity == "file.save":
                folder = tmp_path / editor.FOLDERS[state.selected]
                folder.mkdir(exist_ok=True)
                (folder / f"{state.value}.json").write_text(json.dumps({"name": state.value}))
                state.stage = "saved"
            else:
                raise AssertionError(identity)
            state.revision += 1
            return snapshot()

    def choose(*_):
        return ChoiceAnswer(choice="content.new_material", confidence=0.95, probabilities={"content.new_material": 0.99})

    monkeypatch.setattr(editor, "EditorClient", Connection)
    monkeypatch.setattr(editor, "select_asset", choose)
    monkeypatch.setattr(session, "ROOT", tmp_path / "locks")
    return state


def start(tmp_path, **kwargs):
    return session.start(
        RunConfig(
            goal="Create a material",
            out=tmp_path / "run",
            act=True,
            editor_socket="/tmp/test.sock",
            workflow="create_asset",
            **kwargs,
        )
    )


def resume(result, **reply):
    return session.resume(result["run_dir"], result["pending"]["request_id"], reply)


def test_supervised_name_then_create_save_and_verify(tmp_path, app):
    first = start(tmp_path)
    assert first["pending"]["reason"] == "text"
    assert len(app.calls) == 2
    final = resume(first, text="Sky")
    assert final["outcome"] == "complete" and final["goal_achieved"]
    assert len(app.calls) == 6
    assert json.loads((tmp_path / "materials/Sky.json").read_text())["name"] == "Sky"
    with pytest.raises(ValueError, match="consumed"):
        resume(first, text="Again")


def test_stale_handoff_never_types(tmp_path, app):
    first = start(tmp_path)
    app.revision += 1
    final = resume(first, text="Sky")
    assert final["pending"]["reason"] == "stale_observation"
    assert len(app.calls) == 2


def test_low_confidence_does_not_create(tmp_path, app, monkeypatch):
    monkeypatch.setattr(
        editor,
        "select_asset",
        lambda *_: ChoiceAnswer(choice="content.new_material", confidence=0.7, probabilities={"content.new_material": 0.9}),
    )
    result = start(tmp_path)
    assert result["pending"]["reason"] == "low_confidence"
    assert len(app.calls) == 1
    approved = resume(result, approve=True)
    assert approved["pending"]["reason"] == "text"
    assert approved["editor"]["supervisor_override"]


def test_existing_asset_never_overwritten(tmp_path, app):
    first = start(tmp_path)
    (tmp_path / "materials").mkdir()
    path = tmp_path / "materials/Sky.json"
    path.write_text("existing")
    final = resume(first, text="Sky")
    assert final["outcome"] == "error"
    assert path.read_text() == "existing" and len(app.calls) == 2


def test_observation_does_not_act(tmp_path, app):
    result = session.start(
        RunConfig(goal="Create material", out=tmp_path / "run", editor_socket="/tmp/test.sock", workflow="create_asset")
    )
    assert result["outcome"] == "observed" and app.calls == []


def test_editor_reload_rejects_old_text_reply(tmp_path, app):
    first = start(tmp_path)
    app.instance = "reloaded"
    final = resume(first, text="Sky")
    assert final["outcome"] == "error"
    assert "process or project changed" in final["editor"]["error"]
    assert len(app.calls) == 2


def test_clipped_menu_is_not_sent_to_laya():
    observation = {
        "state": {
            "controls": [
                {"id": "content.new_material", "label": "Material", "enabled": True, "blocked": False},
                {"id": "content.new_shape", "label": "Shape", "enabled": True, "blocked": False},
            ]
        }
    }
    with pytest.raises(ValueError, match="clipped"):
        editor.select_asset(None, "Create a scene", observation)
