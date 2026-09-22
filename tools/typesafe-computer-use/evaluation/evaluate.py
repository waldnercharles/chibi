"""Live, repeatable model-quality check; never sends editor input."""

import argparse
import json
import statistics
import time
from pathlib import Path

from typesafe_computer_use.editor import FOLDERS, INSTRUCTIONS, MIN_CONFIDENCE, EditorClient, controls
from typesafe_computer_use.laya import LayaClient, compact_state

parser = argparse.ArgumentParser()
parser.add_argument("--socket", required=True, help="editor with the New Asset menu open")
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
observation = EditorClient(args.socket).request("snapshot")
menu = {k: v["label"] for k, v in controls(observation).items() if k in FOLDERS}
assert len(menu) == 9, "Open the full New Asset menu first"
cases = json.loads(Path(__file__).with_name("assets.json").read_text())
results = []
args.out.parent.mkdir(parents=True, exist_ok=True)
with LayaClient(trace=args.out.with_suffix(".requests.jsonl")) as client:
    for index, case in enumerate(cases):
        for order in ("original", "reversed"):
            choices = dict(list(menu.items())[:: 1 if order == "original" else -1])
            for mode in ("generic", "structured"):
                state = case["goal"]
                instruction = INSTRUCTIONS
                if mode == "generic":
                    state = compact_state(
                        {
                            "goal": case["goal"],
                            "frontmost_app": "chibi",
                            "screen_items_in_reading_order": [{"text": label} for label in menu.values()],
                            "previous_actions": ["clicked '+ New'"],
                        }
                    )
                    instruction = "Which visible item should be clicked next to advance the goal?"
                started = time.monotonic()
                answer = client._ask(state, instruction, choices)
                row = {
                    **case,
                    "mode": mode,
                    "order": order,
                    "choice": answer.choice,
                    "confidence": answer.confidence,
                    "probabilities": answer.probabilities,
                    "seconds": time.monotonic() - started,
                }
                row["correct"] = answer.choice == case["expected"]
                row["accepted"] = answer.confidence >= MIN_CONFIDENCE
                results.append(row)
        print(f"{index + 1}/{len(cases)} {case['split']}", flush=True)
summary = {}
for mode in ("generic", "structured"):
    summary[mode] = {}
    for split in ("development", "held_out", "challenge"):
        rows = [r for r in results if r["mode"] == mode and r["split"] == split]
        accepted = [r for r in rows if r["accepted"]]
        answerable = [r for r in rows if r["expected"] is not None]
        summary[mode][split] = {
            "cases": len(rows),
            "correct_choices": sum(r["correct"] for r in answerable),
            "answerable": len(answerable),
            "accepted": len(accepted),
            "accepted_errors": sum(not r["correct"] for r in accepted),
            "median_confidence": statistics.median(r["confidence"] for r in rows),
            "median_seconds": statistics.median(r["seconds"] for r in rows),
        }
args.out.write_text(
    json.dumps({"threshold": MIN_CONFIDENCE, "observation": observation, "summary": summary, "results": results}, indent=2)
)
print(json.dumps(summary, indent=2))
