# MatrixGame - licensed under GPLv2 or any later version.
"""Exercise staging refusals with synthetic files, without starting an exporter or game."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "prepare-calcvis-playtest.ps1"
POWERSHELL = shutil.which("powershell.exe")


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(POWERSHELL, "Windows PowerShell is required")
class CalcVisPreparation(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "calcvis-preparation-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="case-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.script = self.tools / SCRIPT.name
        shutil.copyfile(SCRIPT, self.script)
        self.destination = self.root / "prepared"

    def make_receipt(self):
        self.destination.mkdir()
        names = (
            "CalcVisExport.exe", "MatrixGame.exe", "CalcVisObserver.exe", "d3dx9_43.dll",
            "CFG/standalone.txt", "CFG/robots/data.txt", "CFG/robots/iface.txt",
            "DATA/robots.pkg", "Matrix/Map/calcvis-ridge.cmap", "pixel.png",
        )
        files = []
        for name in names:
            path = self.destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic fixture; never execute\n")
            files.append(dict(Path=name, SHA256=hashlib.sha256(path.read_bytes()).hexdigest().upper()))
        return dict(Version=1, Profile="calcvis-ridge", Configuration="Debug", ConfigurationMode="Text", Files=files)

    def save_receipt(self, record):
        (self.destination / "calcvis-preparation.json").write_text(json.dumps(record), encoding="utf-8")

    def assert_refusal(self, expected, verify=True):
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        # Load ordinary cmdlets, then reproduce CI's unavailable script-module lookup.
        command = (
            "Get-Date | Out-Null; Get-Item . | Out-Null; "
            "Remove-Item Function:\\Get-FileHash -ErrorAction SilentlyContinue; "
            "$env:PSModulePath = 'nonexistent-core-only-modules'; "
            "$PSModuleAutoLoadingPreference = 'None'; "
            "& " + quote(self.script) + " -Destination " + quote(self.destination)
        )
        if verify:
            command += " -VerifyOnly"
        environment = dict(os.environ)
        # Windows PowerShell must use its own modules when invoked by pwsh CI.
        environment.pop("PSModulePath", None)
        result = subprocess.run([POWERSHELL, "-NoProfile", "-NonInteractive", "-Command", command],
                                cwd=self.root, env=environment, text=True, capture_output=True, timeout=20)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(expected, result.stderr)
        after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_rooted_receipt_path_is_refused(self):
        record = self.make_receipt()
        record["Files"][0]["Path"] = str(self.root / "outside.exe")
        self.save_receipt(record)
        self.assert_refusal("Receipt file paths must be relative")

    def test_escaping_receipt_path_is_refused(self):
        record = self.make_receipt()
        record["Files"][0]["Path"] = "../outside.exe"
        self.save_receipt(record)
        self.assert_refusal("Receipt path leaves the prepared export")

    def test_changed_listed_file_is_refused(self):
        record = self.make_receipt()
        self.save_receipt(record)
        (self.destination / "pixel.png").write_bytes(b"changed")
        self.assert_refusal("Prepared file changed: pixel.png")

    def test_missing_listed_file_is_refused(self):
        record = self.make_receipt()
        (self.destination / "pixel.png").unlink()
        self.save_receipt(record)
        self.assert_refusal("pixel.png")

    def test_optional_audio_is_refused_before_exporter_execution(self):
        record = self.make_receipt()
        self.save_receipt(record)
        for name in ("sound.pkg", "voices.pkg", "music.pkg"):
            with self.subTest(name=name):
                path = self.destination / "DATA" / name
                path.write_bytes(b"synthetic optional package")
                self.assert_refusal("The controlled profile must remain silent")
                path.unlink()

    def test_invalid_receipt_is_refused(self):
        record = self.make_receipt()
        record["Version"] = 2
        self.save_receipt(record)
        self.assert_refusal("Invalid preparation receipt")

    def test_existing_destination_is_refused_without_changes(self):
        self.destination.mkdir()
        (self.destination / "sentinel.txt").write_bytes(b"preserve")
        self.assert_refusal("Destination already exists", verify=False)

    def test_wrong_cache_configuration_is_refused_without_export(self):
        build = self.root / "build" / "mingw-debug-exe"
        build.mkdir(parents=True)
        defaults = ["CMAKE_BUILD_TYPE:STRING=Debug", "MATRIXGAME_BUILD_DLL:BOOL=OFF", "MATRIXGAME_CHEATS:BOOL=OFF"]
        for index, wrong in enumerate(("CMAKE_BUILD_TYPE:STRING=Release", "MATRIXGAME_BUILD_DLL:BOOL=ON", "MATRIXGAME_CHEATS:BOOL=ON")):
            with self.subTest(wrong=wrong):
                lines = list(defaults)
                lines[index] = wrong
                (build / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
                self.assert_refusal("Build configuration differs", verify=False)
                self.assertFalse(self.destination.exists())

    def test_missing_destination_parent_is_refused(self):
        self.destination = self.root / "missing" / "prepared"
        self.assert_refusal("Destination parent must already exist", verify=False)


if __name__ == "__main__":
    unittest.main()
