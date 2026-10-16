# MatrixGame - licensed under GPLv2 or any later version.
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
WIZARD = PROJECT / "tools/build-wizard.ps1"
SENTINEL = "Synthetic downloader called"

# Every download fails loudly, so a test can tell which preparation reached the network.
DOWNLOADER = f"""function Save-VerifiedDownload([string]$Uri, [string]$Path, [string]$Sha256) {{
    throw "{SENTINEL} for $Uri"
}}
"""


def quote(path):
    return "'" + str(path).replace("'", "''") + "'"


class BuildWizardTests(unittest.TestCase):
    """Runs wizard functions, loaded by dot-sourcing, in a fresh synthetic checkout."""

    def setUp(self):
        self.shell = shutil.which("pwsh") or shutil.which("powershell")
        if self.shell is None:
            self.skipTest("The supported Windows setup requires PowerShell")
        base = PROJECT / "build/setup-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=base)
        self.root = Path(self.temporary.name)
        tools = self.root / "tools"
        tools.mkdir()
        for name in ("build-wizard.ps1", "setup-toolchain.ps1", "setup-cmake.ps1", "setup-directx-runtime.ps1"):
            shutil.copyfile(PROJECT / "tools" / name, tools / name)
        (tools / "download-verified.ps1").write_text(DOWNLOADER, encoding="utf-8")
        # The setup scripts only look for 7z on PATH; this one is never run by these tests.
        self.extractor = self.root / "extractor"
        self.extractor.mkdir()
        (self.extractor / "7z.cmd").write_text("@exit /b 2\n", encoding="ascii")
        self.marker = self.root / "seven-zip-requested"

    def tearDown(self):
        self.temporary.cleanup()

    def run_wizard(self, command, extractor_available=True):
        if extractor_available:
            seven_zip = f"Set-Content -LiteralPath {quote(self.marker)} -Value 1; $env:PATH = {quote(self.extractor)} + ';' + $env:PATH"
        else:
            seven_zip = f"Set-Content -LiteralPath {quote(self.marker)} -Value 1; throw 'Synthetic 7-Zip resolution stopped'"
        script = (f". {quote(self.root / 'tools/build-wizard.ps1')}\n"
                  f"function Use-SevenZip {{ {seven_zip} }}\n"
                  f"{command}\n")
        return subprocess.run([self.shell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                               "-Command", script], capture_output=True, text=True, timeout=60)

    def test_cmake_is_resolved_before_the_compiler(self):
        source = WIZARD.read_text(encoding="utf-8")
        self.assertLess(source.index("Resolve-Component 'CMake'"), source.index("Resolve-Component 'MinGW x86 compiler'"))
        self.assertLess(source.index("Resolve-Component 'CMake'"), source.index("Resolve-Component 'DirectX SDK"))

    def test_supplied_toolchain_archive_does_not_download_cmake(self):
        archive = self.root / "winlibs.7z"
        archive.write_bytes(b"synthetic invalid toolchain archive")
        result = self.run_wizard(f"Install-PinnedToolchain {quote(archive)}")
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(SENTINEL, output)
        self.assertIn("Toolchain archive checksum mismatch", output)
        self.assertTrue(self.marker.exists())
        self.assertFalse((self.root / ".tools/cmake-4.4.4-windows-i386").exists())
        self.assertFalse((self.root / ".tools/downloads").exists())

    def test_invalid_cached_runtime_is_rejected_rather_than_staged(self):
        cache = self.root / ".tools/directx-x86/d3dx9_43.dll"
        cache.parent.mkdir(parents=True)
        cache.write_bytes(b"synthetic truncated runtime")
        game = self.root / "game"
        game.mkdir()
        result = self.run_wizard(f"Copy-DirectXRuntime {quote(game)}", extractor_available=False)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match the pinned x86 runtime", output)
        self.assertFalse((game / "d3dx9_43.dll").exists())
        self.assertFalse(self.marker.exists())
        self.assertEqual(cache.read_bytes(), b"synthetic truncated runtime")

    def test_missing_runtime_is_prepared_before_staging(self):
        game = self.root / "game"
        game.mkdir()
        result = self.run_wizard(f"Copy-DirectXRuntime {quote(game)}", extractor_available=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Synthetic 7-Zip resolution stopped", result.stdout + result.stderr)
        self.assertTrue(self.marker.exists())
        self.assertFalse((game / "d3dx9_43.dll").exists())


if __name__ == "__main__":
    unittest.main()
