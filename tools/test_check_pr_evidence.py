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
- Visual evidence: https://github.com/user-attachments/assets/123e4567-e89b-12d3-a456-426614174000
- Interaction trace: Opened the editor, selected the panel, and captured the result.
"""


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
    )


class EvidenceValidatorTests(unittest.TestCase):
    def test_accepts_automated_evidence(self):
        self.assertEqual(run(VALID_AUTOMATED).returncode, 0)

    def test_accepts_github_user_attachment_for_visual_evidence(self):
        self.assertEqual(run(VALID_VISUAL).returncode, 0)

    def test_rejects_external_artifact_url(self):
        result = run(
            VALID_VISUAL.replace(
                "https://github.com/user-attachments/assets/123e4567-e89b-12d3-a456-426614174000",
                "https://example.com/capture.mp4",
            )
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GitHub user-attachment", result.stdout)

    def test_rejects_attachment_url_with_query_or_trailing_text(self):
        result = run(
            VALID_VISUAL.replace("426614174000", "426614174000?download=1")
        )
        self.assertNotEqual(result.returncode, 0)

    def test_requires_interaction_trace_for_visual_evidence(self):
        result = run(
            VALID_VISUAL.replace(
                "Opened the editor, selected the panel, and captured the result.",
                "Not applicable — headless render only",
            )
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("interaction trace", result.stdout)

    def test_rejects_multiple_body_arguments_as_usage_error(self):
        result = run("body", "extra")
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
