"""Extract only archived patches/logs, verify their original hashes; execute nothing."""
from pathlib import Path
import base64
import hashlib
import json
import sys

root = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
if output.exists():
    raise SystemExit("Choose a new output directory")
rows = json.loads((root / "transport.json").read_text())["files"]
verified = []
for row in rows:
    relative = Path(row["decoded_path"])
    if relative.is_absolute() or ".." in relative.parts:
        raise SystemExit("Invalid archived relative path")
    data = base64.b64decode((root / row["transport_path"]).read_bytes())
    if len(data) != row["decoded_bytes"] or hashlib.sha256(data).hexdigest() != row["decoded_sha256"]:
        raise SystemExit("Archived content differs: " + str(relative))
    verified.append((relative, data))
output.mkdir(parents=True)
for relative, data in verified:
    target = output / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
print("Extracted", len(verified), "verified patches/logs; no source was executed")
