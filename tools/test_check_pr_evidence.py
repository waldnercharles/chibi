#!/usr/bin/env python3
"""Executable CLI tests for the pull-request evidence validator."""

import subprocess
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check_pr_evidence.py")
VALID_AUTOMATED = """## Evidence
- Category: `automated`
- Feature proof: The validator checks evidence metadata.
- Tests: CLI tests pass.
- Visual evidence: Not applicable — non-visual change
- Interaction trace: Not applicable — non-visual change
"""
VALID_VISUAL = """## Evidence
- Category: `visual`
- Feature proof: The editor renders the changed panel after interaction.
- Tests: Integration test passes.
- Visual evidence: Local capture inspected; media remains local.
- Interaction trace: Opened the editor, selected the panel, and captured the result.
"""


def run(body: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), body], capture_output=True, text=True, check=False
    )


class EvidenceValidatorTests(unittest.TestCase):
    def test_accepts_automated_evidence(self):
        self.assertEqual(run(VALID_AUTOMATED).returncode, 0)

    def test_accepts_visual_evidence_without_an_attachment(self):
        self.assertEqual(run(VALID_VISUAL).returncode, 0)

    def test_visual_details_are_optional(self):
        body = VALID_VISUAL.replace(
            "- Visual evidence: Local capture inspected; media remains local.\n",
            "",
        ).replace(
            "- Interaction trace: Opened the editor, selected the panel, and captured the result.\n",
            "",
        )
        self.assertEqual(run(body).returncode, 0)


if __name__ == "__main__":
    unittest.main()
