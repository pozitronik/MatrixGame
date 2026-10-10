# MatrixGame - licensed under GPLv2 or any later version.
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class CMakeSetupTests(unittest.TestCase):
    def test_bad_archive_is_rejected_before_extraction(self):
        shell = shutil.which("pwsh") or shutil.which("powershell")
        if shell is None:
            self.skipTest("The supported Windows setup requires PowerShell")
        base = PROJECT / "build/setup-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            script = root / "tools/setup-cmake.ps1"
            shutil.copyfile(PROJECT / "tools/setup-cmake.ps1", script)
            archive = root / "invalid.zip"
            archive.write_bytes(b"synthetic invalid archive")
            result = subprocess.run([shell, "-NoProfile", "-File", str(script),
                                     "-ArchivePath", str(archive)], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CMake archive checksum mismatch", result.stdout + result.stderr)
            self.assertFalse((root / ".tools").exists())
