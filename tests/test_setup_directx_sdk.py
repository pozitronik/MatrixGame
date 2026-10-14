# MatrixGame - licensed under GPLv2 or any later version.
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
ARCHIVE = b"synthetic caller-owned installer"


def compact(result):
    """Drops all whitespace, because PowerShell wraps error records at the console width, even inside words."""
    return "".join((result.stdout + result.stderr).split())


class DirectXSdkSetupTests(unittest.TestCase):
    def setUp(self):
        self.shell = shutil.which("pwsh") or shutil.which("powershell")
        if self.shell is None:
            self.skipTest("The supported Windows setup requires PowerShell")
        base = PROJECT / "build/setup-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=base)
        self.root = Path(self.temporary.name)
        (self.root / "tools").mkdir()
        for name in ("setup-directx-sdk.ps1", "download-verified.ps1"):
            shutil.copyfile(PROJECT / "tools" / name, self.root / "tools" / name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_setup(self, archive):
        return subprocess.run([self.shell, "-NoProfile", "-File", str(self.root / "tools/setup-directx-sdk.ps1"),
                               "-ArchivePath", str(archive)], capture_output=True, text=True, timeout=60)

    def assert_owned_archive_kept(self, directory):
        stale = self.root / ".tools/directx-sdk/DXSDK/Include/stale.h"
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b"stale subset")
        archive = self.root / directory / "DXSDK_Jun10.exe"
        archive.parent.mkdir(parents=True, exist_ok=True)
        archive.write_bytes(ARCHIVE)
        result = self.run_setup(archive)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mustnotbeinside", compact(result))
        self.assertEqual(archive.read_bytes(), ARCHIVE)
        self.assertTrue(stale.exists())

    def test_archive_inside_sdk_directory_is_refused_before_cleanup(self):
        self.assert_owned_archive_kept(".tools/directx-sdk")

    def test_archive_inside_staging_directory_is_refused_before_cleanup(self):
        self.assert_owned_archive_kept(".tools/directx-sdk.partial")

    def test_bad_archive_is_kept_and_not_extracted(self):
        if shutil.which("7z") is None:
            self.skipTest("Preparing the DirectX SDK subset requires 7-Zip")
        archive = self.root / "DXSDK_Jun10.exe"
        archive.write_bytes(ARCHIVE)
        result = self.run_setup(archive)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("DirectXSDKarchivechecksummismatch", compact(result))
        self.assertEqual(archive.read_bytes(), ARCHIVE)
        self.assertFalse((self.root / ".tools/directx-sdk").exists())
        self.assertFalse((self.root / ".tools/directx-sdk.partial").exists())


if __name__ == "__main__":
    unittest.main()
