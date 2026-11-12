"""Check complete Web entry admission without starting the Web runtime."""

import os
from contextlib import contextmanager
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class WebEntryContract(unittest.TestCase):
    @contextmanager
    def controlled_entry(self):
        script = Path(__file__).resolve().parent / "run-web.sh"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tests").mkdir()
            shutil.copy2(script, root / "tests/run-web.sh")
            (root / "web/node_modules").mkdir(parents=True)
            (root / "bin").mkdir()
            npm = root / "bin/npm"
            npm.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$SELECTION_CAPTURE"\n')
            npm.chmod(0o755)
            capture = root / "arguments"
            environment = dict(
                os.environ,
                PATH=str(root / "bin") + os.pathsep + os.environ["PATH"],
                SELECTION_CAPTURE=str(capture),
            )
            yield root / "tests/run-web.sh", capture, environment

    def test_selectors_are_rejected_before_runtime(self):
        with self.controlled_entry() as (script, capture, environment):
            result = subprocess.run(
                ["bash", str(script), "--testPathPatterns", "missing"],
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("does not accept selectors", result.stderr)
            self.assertFalse(capture.exists())

    def test_full_entry_pins_one_worker_without_selectors(self):
        with self.controlled_entry() as (script, capture, environment):
            result = subprocess.run(
                ["bash", str(script)],
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(
                capture.read_text().splitlines(), ["test", "--", "--runInBand"]
            )
