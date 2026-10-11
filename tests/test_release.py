# MatrixGame - licensed under GPLv2 or any later version.
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release", PROJECT / "tools/release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
TAG = "2026.10.11"
REVISION = "a" * 40
REPO = "fixture/game"


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        base = PROJECT / "build/release-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.packages = self.root / "packages"
        self.packages.mkdir()

    def archive(self, arch="x86", machine=None, **overrides):
        metadata = {"revision": REVISION, "architecture": arch, "configuration": "Release", "cheats": False}
        metadata.update(overrides)
        binary = bytearray(256)
        binary[:2] = b"MZ"
        struct.pack_into("<I", binary, 60, 64)
        binary[64:68] = b"PE\0\0"
        struct.pack_into("<H", binary, 68, machine or (0x14c if arch == "x86" else 0x8664))
        struct.pack_into("<H", binary, 88, 0x10b if arch == "x86" else 0x20b)
        path = self.packages / ("MatrixGame-windows-" + arch + "-" + REVISION[:12] + ".zip")
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("SOURCE.json", json.dumps(metadata))
            archive.writestr("MatrixGame.exe", binary)
            archive.writestr("README.md", "# Game\n\n## Install\n\nSynthetic instructions: supply robots.pkg.")
        return path

    def checksums(self):
        content = "".join(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n"
                          for path in sorted(self.packages.glob("*.zip")))
        path = self.packages / ("MatrixGame-" + REVISION[:12] + ".sha256")
        path.write_text(content, encoding="ascii")
        return path

    def fixture(self):
        self.archive()
        with zipfile.ZipFile(self.packages / ("MatrixGame-source-" + REVISION[:12] + ".zip"), "w") as archive:
            archive.writestr("fixture.txt", "Synthetic source")
        self.checksums()

    def draft(self, existing=False, notes="Generated notes", changelog=None, published=False, **overrides):
        self.fixture()
        if changelog is not None:
            (self.root / "CHANGELOG.md").write_text(changelog, encoding="utf-8")
        record = {"id": 7, "tag_name": TAG, "target_commitish": REVISION,
                  "draft": not published, "html_url": "https://example.invalid/draft"}
        record.update(overrides)

        def response(repo, endpoint, **kwargs):
            self.assertEqual(repo, REPO)
            if endpoint.startswith("releases?"):
                return [[record]] if existing else [[]]
            if endpoint == "releases/generate-notes":
                return {"body": notes}
            return record.copy()

        return patch.object(release, "api", side_effect=response)

    def test_tags_are_real_dates_and_optional_positive_sequence(self):
        for tag in (TAG, TAG + ".1", "2024.02.29.12"):
            release.validate_tag(tag)
        for tag in ("v" + TAG, "2026.02.29", TAG + ".0", TAG + ".01", TAG + "-rc1", "master", TAG + "\n"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate_tag(tag)

    def test_changelog_uses_only_matching_entry_and_retains_subsections(self):
        text = "## [Unreleased]\n\nFuture\n\n## [" + TAG + "]\n\n### Fixed\n\nA bug.\n\n## 2026.01.01\n\nOld."
        self.assertEqual(release.changelog_notes(text, TAG), "### Fixed\n\nA bug.")
        self.assertEqual(release.changelog_notes("## " + TAG + "\r\n\r\nNotes\r\n", TAG), "Notes")

    def test_missing_empty_and_comment_only_entries_use_fallback(self):
        for text in ("", "## Unreleased\nFuture", "## " + TAG, "## " + TAG + "\n<!-- pending -->"):
            self.assertIsNone(release.changelog_notes(text, TAG))

    def test_duplicate_entries_fail(self):
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            release.changelog_notes(("## " + TAG + "\nNotes\n") * 2, TAG)

    def test_packages_require_source_and_x86_and_accept_real_x64(self):
        self.fixture()
        assets, arches, instructions = release.package_assets(self.packages, REVISION)
        self.assertEqual(len(assets), 3)
        self.assertEqual(arches, ["x86"])
        self.assertIn("robots.pkg", instructions)
        self.archive("x64")
        self.checksums()
        self.assertEqual(release.package_assets(self.packages, REVISION)[1], ["x86", "x64"])
        (self.packages / ("MatrixGame-source-" + REVISION[:12] + ".zip")).unlink()
        with self.assertRaisesRegex(ValueError, "Expected only"):
            release.package_assets(self.packages, REVISION)

    def test_asset_corruption_is_rejected(self):
        self.fixture()
        next(self.packages.glob("*.zip")).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.package_assets(self.packages, REVISION)

    def test_unexpected_assets_are_rejected(self):
        self.fixture()
        (self.packages / "robots.pkg").write_bytes(b"synthetic private resource")
        with self.assertRaisesRegex(ValueError, "Expected only"):
            release.package_assets(self.packages, REVISION)

    def test_unlisted_or_duplicate_checksums_are_rejected(self):
        self.fixture()
        sums = self.checksums()
        original = sums.read_text()
        for content in ("", original + original):
            sums.write_text(content)
            with self.assertRaises(ValueError):
                release.package_assets(self.packages, REVISION)

    def test_wrong_revision_configuration_and_architecture_are_rejected(self):
        self.fixture()
        for overrides in ({"revision": "b" * 40}, {"cheats": True}, {"configuration": "Debug"}, {"machine": 0x8664}):
            with self.subTest(overrides=overrides):
                self.archive(**overrides)
                self.checksums()
                with self.assertRaises(ValueError):
                    release.package_assets(self.packages, REVISION)

    def test_generated_notes_create_only_a_draft_with_exact_commit(self):
        with self.draft() as api, patch.object(release, "verify_tag", return_value=REVISION), patch.object(release, "command") as command:
            release.prepare_draft(self.root, TAG, REPO, self.packages)
            payload = next(call.kwargs["payload"] for call in api.call_args_list if call.args[1] == "releases")
            self.assertIs(payload["draft"], True)
            self.assertEqual(payload["target_commitish"], REVISION)
            self.assertIn("Generated notes", payload["body"])
            self.assertIn("Synthetic instructions", payload["body"])
            self.assertIn("#### Install", payload["body"])
            self.assertEqual(command.call_args.args[:4], ("gh", "release", "upload", TAG))
            self.assertIn("--clobber", command.call_args.args)

    def test_changelog_prevents_generated_notes_request(self):
        with self.draft(changelog="## " + TAG + "\n\nWritten notes") as api, patch.object(release, "verify_tag", return_value=REVISION), patch.object(release, "command"):
            release.prepare_draft(self.root, TAG, REPO, self.packages)
            self.assertFalse(any(call.args[1] == "releases/generate-notes" for call in api.call_args_list))
            payload = next(call.kwargs["payload"] for call in api.call_args_list if call.args[1] == "releases")
            self.assertIn("Written notes", payload["body"])

    def test_rerun_updates_existing_draft_without_changing_publication_state(self):
        with self.draft(existing=True) as api, patch.object(release, "verify_tag", return_value=REVISION), patch.object(release, "command"):
            release.prepare_draft(self.root, TAG, REPO, self.packages)
            writes = [call for call in api.call_args_list if call.kwargs.get("method") == "PATCH"]
            self.assertEqual(len(writes), 1)
            self.assertEqual(set(writes[0].kwargs["payload"]), {"body"})
            self.assertFalse(any(call.args[1] == "releases" for call in api.call_args_list))

    def test_published_or_retargeted_draft_is_never_modified(self):
        for options in ({"published": True}, {"target_commitish": "b" * 40}):
            with self.subTest(options=options), self.draft(existing=True, **options) as api, patch.object(release, "verify_tag", return_value=REVISION), patch.object(release, "command") as command:
                with self.assertRaises(ValueError):
                    release.prepare_draft(self.root, TAG, REPO, self.packages)
                self.assertEqual(len(api.call_args_list), 1)
                command.assert_not_called()

    def test_api_failure_does_not_create_release(self):
        self.fixture()
        with patch.object(release, "verify_tag", return_value=REVISION), patch.object(release, "api", side_effect=RuntimeError("Forbidden")) as api, patch.object(release, "command") as command:
            with self.assertRaises(RuntimeError):
                release.prepare_draft(self.root, TAG, REPO, self.packages)
            self.assertEqual(len(api.call_args_list), 1)
            command.assert_not_called()

    def test_tag_verification_with_real_git_lightweight_annotated_and_off_master(self):
        def git(*args):
            return release.command("git", *args, cwd=self.root)

        git("init", "-q", "--initial-branch=master")
        git("config", "user.name", "Fixture")
        git("config", "user.email", "fixture@example.invalid")
        git("commit", "--allow-empty", "-qm", "Stable")
        stable = git("rev-parse", "HEAD")
        git("tag", TAG)
        git("tag", "-a", TAG + ".1", "-m", "Annotated release")
        git("remote", "add", "origin", str(self.root))
        self.assertEqual(release.verify_tag(self.root, TAG), stable)
        self.assertEqual(release.verify_tag(self.root, TAG + ".1"), stable)
        git("commit", "--allow-empty", "-qm", "Later stable change")
        git("switch", "--detach", TAG)
        self.assertEqual(release.verify_tag(self.root, TAG), stable)
        git("switch", "-qc", "dev")
        git("commit", "--allow-empty", "-qm", "Not promoted")
        git("tag", TAG + ".2")
        with self.assertRaises(subprocess.CalledProcessError):
            release.verify_tag(self.root, TAG + ".2")
        with self.assertRaisesRegex(ValueError, "does not match"):
            release.verify_tag(self.root, TAG)


if __name__ == "__main__":
    unittest.main()
