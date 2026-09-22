#!/usr/bin/env python3
"""Download and verify the standalone English Laya checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
from typing import Iterable

from huggingface_hub import snapshot_download


REPOSITORY = "convaiinnovations/laya"
REVISION = "1c5edc17a7acd8701df6fc341c0d179f1c62c982"
ALLOW_PATTERNS = [
    "model.safetensors",
    "rl_agent_config.json",
    "tokenizer/*",
    "encoder/*",
    "email_utils.py",
    "rl_agent_api.py",
    "rl_common.py",
]
REQUIRED_FILES = (
    "model.safetensors",
    "rl_agent_config.json",
    "tokenizer/tokenizer.json",
    "tokenizer/tokenizer_config.json",
    "encoder/config.json",
)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _fix_tokenizer_config(path: Path) -> None:
    config_path = path / "tokenizer" / "tokenizer_config.json"
    config = json.loads(config_path.read_text())
    changed = False
    if config.get("tokenizer_class") in (None, "TokenizersBackend"):
        config["tokenizer_class"] = "PreTrainedTokenizerFast"
        config.pop("backend", None)
        config.pop("is_local", None)
        changed = True
    extra = config.get("extra_special_tokens")
    if isinstance(extra, list):
        config["extra_special_tokens"] = {f"extra_{index}": token for index, token in enumerate(extra)}
        changed = True
    if changed:
        config_path.write_text(json.dumps(config, indent=2) + "\n")


def _missing(root: Path, files: Iterable[str]) -> list[str]:
    return [name for name in files if not (root / name).is_file()]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(os.environ.get("LAYA_MODEL_DIR", "~/.cache/laya-jev/laya")).expanduser(),
        help="Standalone model directory used by LAYA_MODEL_PATH.",
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=Path(
            os.environ.get(
                "HF_HUB_CACHE",
                str(Path(os.environ.get("HF_HOME", "~/.cache/huggingface")) / "hub"),
            )
        ).expanduser(),
        help="Hugging Face hub cache used while downloading or copying the model.",
    )
    parser.add_argument("--revision", default=os.environ.get("LAYA_MODEL_REVISION", REVISION))
    parser.add_argument("--token", default=os.environ.get("LAYA_HF_TOKEN") or os.environ.get("HF_TOKEN"))
    parser.add_argument(
        "--offline",
        action="store_true",
        help="Copy from the existing Hugging Face cache without contacting the Hub.",
    )
    args = parser.parse_args()

    snapshot = snapshot_download(
        REPOSITORY,
        revision=args.revision,
        cache_dir=args.cache_dir,
        local_files_only=args.offline,
        token=args.token,
        allow_patterns=ALLOW_PATTERNS,
    )
    source = Path(snapshot)
    args.output.mkdir(parents=True, exist_ok=True)
    for pattern in ALLOW_PATTERNS:
        for source_file in source.glob(pattern):
            if not source_file.is_file():
                continue
            destination = args.output / source_file.relative_to(source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                destination.chmod(0o644)
            shutil.copyfile(source_file, destination)
    root = args.output
    missing = _missing(root, REQUIRED_FILES)
    if missing:
        raise SystemExit(f"Downloaded checkpoint is incomplete; missing: {', '.join(missing)}")
    _fix_tokenizer_config(root)
    model = root / "model.safetensors"
    manifest = {
        "repository": REPOSITORY,
        "revision": args.revision,
        "model": str(model.relative_to(root)),
        "bytes": model.stat().st_size,
        "sha256": _sha256(model),
        "required_files": list(REQUIRED_FILES),
    }
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
