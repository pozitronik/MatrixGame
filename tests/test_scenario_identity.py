"""Build-time scenario provenance contracts using a synthetic Git repository."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
GENERATOR = PROJECT / "tests/write_scenario_identity.cmake"
PINNED_CMAKE = PROJECT / ".tools/cmake-4.4.4-windows-i386/bin/cmake.exe"
CMAKE = os.environ.get("MATRIXGAME_TEST_CMAKE") or (
    str(PINNED_CMAKE) if PINNED_CMAKE.is_file() else shutil.which("cmake"))


class ScenarioIdentityTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(CMAKE, "CMake is required for scenario identity contracts")
        base = Path(os.environ.get("MATRIXGAME_IDENTITY_TEST_ROOT", PROJECT / "build/identity-tests"))
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "source"
        self.repo.mkdir()
        self.header = self.root / "output/scenario_build_identity.hpp"
        self.git("init", "-q")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.source = self.repo / "fixture.txt"
        self.source.write_text("initial\n", encoding="utf-8")
        self.git("add", "fixture.txt")
        self.git("commit", "-qm", "Initial fixture")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def generate(self, source=None, *options):
        subprocess.run([CMAKE, f"-DSOURCE_ROOT={source or self.repo}",
                        f"-DOUTPUT_HEADER={self.header}", *options, "-P", str(GENERATOR)],
                       check=True, capture_output=True, text=True)
        return self.header.read_text(encoding="utf-8")

    def assert_identity(self, revision, dirty, source=None, *options):
        text = self.generate(source, *options)
        self.assertIn(f'#define MATRIXGAME_SCENARIO_REVISION "{revision}"\n', text)
        self.assertIn(f"#define MATRIXGAME_SCENARIO_DIRTY {dirty}\n", text)

    def test_commit_and_tracked_edits_refresh_without_reconfiguration(self):
        initial = self.git("rev-parse", "HEAD")
        self.assert_identity(initial, 0)
        self.source.write_text("unstaged\n", encoding="utf-8")
        self.assert_identity(initial, 1)
        self.git("add", "fixture.txt")
        self.assert_identity(initial, 1)
        self.git("commit", "-qm", "Changed fixture")
        committed = self.git("rev-parse", "HEAD")
        self.assertNotEqual(initial, committed)
        self.assert_identity(committed, 0)

    def test_unchanged_identity_preserves_header_timestamp(self):
        self.generate()
        # Set an old timestamp to make an unnecessary rewrite observable even on
        # filesystems whose timestamp resolution is coarser than the test runtime.
        os.utime(self.header, (1_000_000_000, 1_000_000_000))
        timestamp = self.header.stat().st_mtime_ns
        self.generate()
        self.assertEqual(timestamp, self.header.stat().st_mtime_ns)

    def test_untracked_files_do_not_change_tracked_dirty_flag(self):
        (self.repo / "untracked.txt").write_text("not compiled\n", encoding="utf-8")
        self.assert_identity(self.git("rev-parse", "HEAD"), 0)

    def test_linked_worktree_uses_its_own_head_and_tracked_edits(self):
        worktree = self.root / "linked-source"
        self.git("worktree", "add", "--detach", str(worktree), "HEAD")
        try:
            self.assertTrue((worktree / ".git").is_file())
            self.assert_identity(self.git("rev-parse", "HEAD"), 0, worktree)
            (worktree / "fixture.txt").write_text("worktree edit\n", encoding="utf-8")
            self.assert_identity(self.git("rev-parse", "HEAD"), 1, worktree)
        finally:
            self.git("worktree", "remove", "--force", str(worktree))

    def test_source_archive_does_not_inherit_parent_repository_identity(self):
        archive = self.repo / "archive"
        archive.mkdir()
        self.assert_identity("unknown", -1, archive)

    def test_build_without_git_keeps_unknown_provenance(self):
        self.assert_identity("unknown", -1, None, "-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE")


if __name__ == "__main__":
    unittest.main()
