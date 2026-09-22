"""Structured Chibi observations and a bounded, supervised asset-creation workflow.

Laya selects the asset type from the editor's actual menu. The executor owns
navigation, preconditions and readback. Codex supplies the name at a checkpoint.
This workflow does not ask the classifier to plan arbitrary GUI operations.
"""

from __future__ import annotations

import json
import socket
import time
import uuid
from pathlib import Path

from .laya import LayaClient

INSTRUCTIONS = "Select the asset type the user wants to create."
MIN_CONFIDENCE = 0.8
FOLDERS = {
    "content.new_scene": "scenes",
    "content.new_prototype": "prototypes",
    "content.new_behavior": "behaviors",
    "content.new_library": "libraries",
    "content.new_shader_library": "shader-libraries",
    "content.new_event": "events",
    "content.new_material": "materials",
    "content.new_field": "fields",
    "content.new_shape": "shapes",
}


class EditorClient:
    def __init__(self, path):
        self.path = str(path)

    def request(self, operation, observation=None, **values):
        packet = {"operation": operation, **values}
        if observation:
            packet.update(pid=observation["pid"], instance=observation["instance"], revision=observation["revision"])
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(5)
            connection.connect(self.path)
            connection.sendall((json.dumps(packet) + "\n").encode())
            with connection.makefile("rb") as stream:
                line = stream.readline(1024 * 1024)
        result = json.loads(line)
        if result.get("contract") != 1 or not result.get("ok"):
            raise ValueError(result.get("error", "Invalid editor response"))
        return result


def controls(observation):
    return {item["id"]: item for item in observation["state"]["controls"] if item["enabled"] and not item["blocked"]}


def select_asset(client, goal, observation):
    candidates = {key: item["label"] for key, item in controls(observation).items() if key in FOLDERS}
    if set(candidates) != set(FOLDERS):
        raise ValueError("Expected the complete New Asset menu; a clipped menu cannot be classified safely")
    # A single typed classification. No chopped group labels or guessed OCR roles.
    return client._ask(goal, INSTRUCTIONS, candidates)


def run_editor(cfg, state, checkpoint, pending=None, reply=None):
    started = time.monotonic()
    connection = EditorClient(cfg.editor_socket)
    state.outcome = "running"
    state.pending = None
    work = state.editor
    observation = None

    def pause(reason, **extra):
        state.outcome = "paused"
        state.pending = {"request_id": str(uuid.uuid4()), "reason": reason, "observation": observation, **extra}

    def act(operation, identity, **extra):
        nonlocal observation
        if len(state.history) >= cfg.steps:
            raise ValueError("Editor action limit reached")
        checkpoint(state)  # interrupted actions are never replayed automatically
        before = time.monotonic()
        observation = connection.request(operation, observation, id=identity, **extra)
        state.history.append(f"{operation} {identity}")
        state.timings.append({"operation": operation, "seconds": time.monotonic() - before})
        checkpoint(state)

    try:
        observation = connection.request("snapshot")
        binding = {
            "pid": observation["pid"],
            "instance": observation["instance"],
            "content_root": observation["state"]["content_root"],
        }
        if work.setdefault("binding", binding) != binding:
            raise ValueError("Editor process or project changed; start a new task")
        if not cfg.act:
            state.outcome = "observed"
            return
        if pending and not (reply or {}).get("retry"):
            old = pending["observation"]
            if any(observation[key] != old[key] for key in ("pid", "instance", "revision")):
                pause("stale_observation")
                return
        stage = work.get("stage", "menu")
        if stage == "menu":
            if "content.create" not in controls(observation):
                raise ValueError("Open the Files navigator with no popup before starting asset creation")
            act("click", "content.create")
            work["stage"] = stage = "choose"
        if stage == "choose":
            if pending and pending["reason"] == "low_confidence" and (reply or {}).get("approve"):
                decision = work["decision"]
                work["supervisor_override"] = True
            else:
                before = time.monotonic()
                with LayaClient(trace=cfg.out / "laya-requests.jsonl") as model:
                    answer = select_asset(model, cfg.goal, observation)
                decision = {"choice": answer.choice, "confidence": answer.confidence, "probabilities": answer.probabilities}
                work["decision"] = decision
                state.timings.append({"operation": "decide", "seconds": time.monotonic() - before})
                if answer.confidence < max(MIN_CONFIDENCE, cfg.min_confidence):
                    pause("low_confidence", decision=decision)
                    return
            work["asset_label"] = controls(observation)[decision["choice"]]["label"]
            act("click", decision["choice"])
            work["stage"] = stage = "name"
            pause("text", field="scene.new.name", label="Asset name", max_bytes=127)
            return
        if stage == "name":
            create_button = next((c for c in observation["state"]["controls"] if c["id"] == "scene.new.create"), None)
            if not create_button or create_button["label"] != f"Create {work['asset_label']}":
                raise ValueError("The asset creation dialog changed")
            name = (reply or {}).get("text")
            if not isinstance(name, str) or not name.strip() or len(name.encode()) > 127:
                pause("text", field="scene.new.name", label="Asset name", max_bytes=127)
                return
            # Match the editor's filename contract and refuse an existing artifact.
            if any(not (c.isascii() and (c.isalnum() or c in " _-")) for c in name):
                raise ValueError("Asset names use letters, numbers, spaces, hyphens or underscores")
            folder = FOLDERS[work["decision"]["choice"]]
            artifact = Path(observation["state"]["content_root"]) / folder / f"{name}.json"
            if artifact.exists():
                raise ValueError("An asset with this name already exists")
            act("type", "scene.new.name", text=name)
            field = controls(observation).get("scene.new.name")
            if not field or field["value"] != name:
                raise ValueError("Asset name readback did not match")
            work.update(name=name, artifact=str(artifact))
            act("click", "scene.new.create")
            if "scene.new.name" in controls(observation):
                raise ValueError(observation["state"]["message"] or "Editor did not create the asset")
            work["stage"] = stage = "save"
        if stage == "save":
            if work["decision"]["choice"] != "content.new_scene" and observation["state"]["document"] != work["name"]:
                raise ValueError("The active document changed before saving")
            # Scene creation already persists its file. Other new assets need Save.
            artifact = Path(work["artifact"])
            if not artifact.exists():
                act("click", "file.menu")
                act("click", "file.save")
            data = json.loads(artifact.read_text())
            if data.get("name") != work["name"]:
                raise ValueError("Saved artifact name did not match")
            if (
                work["decision"]["choice"] not in {"content.new_scene", "content.new_prototype"}
                and observation["state"]["graph_dirty"]
            ):
                raise ValueError("Editor still reports unsaved graph changes")
            work["stage"] = "complete"
            state.outcome = "complete"
    except Exception as error:
        state.outcome = "error"
        work["error"] = str(error)
    finally:
        work["observation"] = observation
        state.timings.append({"operation": "invocation", "seconds": time.monotonic() - started})
        result = {
            "goal": cfg.goal,
            "workflow": cfg.workflow,
            "outcome": state.outcome,
            "goal_achieved": state.outcome == "complete",
            "pending": state.pending,
            "history": state.history,
            "timings": state.timings,
            "editor": work,
        }
        (cfg.out / "run.json").write_text(json.dumps(result, indent=2))
        checkpoint(state)
