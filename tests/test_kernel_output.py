"""Reject unsafe evidence destinations before configuring a kernel fixture."""

from pathlib import Path
import tempfile
import unittest

from kernel_boundary import validate_output_path


class KernelOutputContract(unittest.TestCase):
    def test_relative_output_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "absolute"):
            validate_output_path(Path("relative-results"), [])

    def test_existing_file_directory_and_symlink_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            (parent / "file").write_text("retained")
            (parent / "directory").mkdir()
            (parent / "link").symlink_to(parent / "absent")
            for name in ("file", "directory", "link"):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, "already exists"):
                        validate_output_path(parent / name, [])
            self.assertEqual((parent / "file").read_text(), "retained")
            self.assertFalse((parent / "absent").exists())

    def test_owner_and_dependency_sources_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            sources = [parent / name for name in ("owner", "dependency")]
            for source in sources:
                source.mkdir()
            for source in sources:
                with self.subTest(source=source.name):
                    with self.assertRaisesRegex(
                        ValueError, "outside owner and dependency"
                    ):
                        validate_output_path(source / "results", sources)
                    self.assertFalse((source / "results").exists())

    def test_parent_symlink_into_sources_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            source = parent / "owner"
            source.mkdir()
            (parent / "alias").symlink_to(source, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "outside owner and dependency"):
                validate_output_path(parent / "alias/results", [source])
            self.assertFalse((source / "results").exists())

    def test_missing_parent_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "parent must exist"):
                validate_output_path(Path(temporary) / "missing/results", [])

    def test_fresh_external_output_resolves_parent_without_creating_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            actual = parent / "actual"
            actual.mkdir()
            alias = parent / "alias"
            alias.symlink_to(actual, target_is_directory=True)
            result = validate_output_path(alias / "results", [parent / "owner"])
            self.assertEqual(result, actual / "results")
            self.assertFalse(result.exists())
