# MatrixGame - licensed under GPLv2 or any later version.
"""Exercise preparation/restoration with synthetic files and a mocked process only."""
import json
import hashlib
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "playtest-outcomes.ps1"
POWERSHELL = shutil.which("powershell.exe")


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(POWERSHELL, "Windows PowerShell is required")
class OutcomePreparation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="matrix-outcome-preparation-")
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        self.game = self.build / "MatrixGame"
        self.cfg = self.game / "CFG"
        (self.cfg / "robots").mkdir(parents=True)
        (self.game / "DATA").mkdir()
        (self.game / "DATA" / "robots.pkg").write_bytes(b"synthetic package, never opened by the game")
        for name in ("data.txt", "iface.txt"):
            (self.cfg / "robots" / name).write_text("synthetic text", encoding="utf-8")
        self.original = b"\xff\xfe" + "// preserve exact bytes\r\nMap=custom.cmap\r\n".encode("utf-16-le")
        (self.cfg / "standalone.txt").write_bytes(self.original)
        (self.cfg / "robots.dat").write_bytes(b"original packed bytes")
        pe = bytearray(128)
        struct.pack_into("<H", pe, 0, 0x5A4D)
        struct.pack_into("<I", pe, 60, 64)
        struct.pack_into("<IH", pe, 64, 0x4550, 0x14C)
        (self.game / "MatrixGame.exe").write_bytes(pe)
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\nMATRIXGAME_BUILD_DLL:BOOL=OFF\nMATRIXGAME_CHEATS:BOOL=OFF\n",
            encoding="utf-8",
        )

    def run_helper(self, *args, fail_start=False, leftover=False, running=False, wait_failure=False):
        # No process is created. The mock observes the staged files, then simulates a
        # preference save or a launch failure before the helper restores its backups.
        observe = quote(self.build / "observed.json")
        mock = r"""
# Keep the ordinary cmdlets available while reproducing unavailable script
# functions under Windows PowerShell's inherited Core-only module lookup.
Get-Date | Out-Null
Get-Item . | Out-Null
Remove-Item Function:\Get-FileHash -ErrorAction SilentlyContinue
$env:PSModulePath = 'nonexistent-core-only-modules'
$PSModuleAutoLoadingPreference = 'None'
$global:outcomeCalls = 0
$global:manifestBeforePacked = $null
function Remove-Item {
    [CmdletBinding()] param([string]$LiteralPath, [string]$Path, [switch]$Force)
    if ($LiteralPath -and (Split-Path -Leaf $LiteralPath) -eq 'robots.dat' -and
        (Split-Path -Leaf (Split-Path -Parent $LiteralPath)) -eq 'CFG') {
        $backup = Join-Path (Split-Path -Parent $LiteralPath) '.native-outcome-backup'
        $global:manifestBeforePacked = (Test-Path -LiteralPath (Join-Path $backup 'state.json')) -and
            (Test-Path -LiteralPath (Join-Path $backup 'standalone.txt')) -and
            (Test-Path -LiteralPath (Join-Path $backup 'robots.dat'))
    }
    if ($LiteralPath) { Microsoft.PowerShell.Management\Remove-Item -LiteralPath $LiteralPath -Force:$Force }
    else { Microsoft.PowerShell.Management\Remove-Item -Path $Path -Force:$Force }
}
function Get-Process {
    [CmdletBinding()] param([string]$Name)
    ++$global:outcomeCalls
    if (RUNNING -or (LEFTOVER -and $global:outcomeCalls -gt 1)) { [pscustomobject]@{Id = 456} }
}
function Start-Process {
    [CmdletBinding()] param([string]$FilePath, [string]$WorkingDirectory, [switch]$PassThru)
    $cfg = Join-Path $WorkingDirectory 'CFG'
    $observation = @{
        Settings = [IO.File]::ReadAllText((Join-Path $cfg 'standalone.txt'))
        Packed = Test-Path -LiteralPath (Join-Path $cfg 'robots.dat')
        Executable = $FilePath
        ManifestBeforePacked = $global:manifestBeforePacked
    }
    $observation | ConvertTo-Json | Set-Content -LiteralPath OBSERVE -Encoding UTF8
    if (FAIL_START) { throw 'synthetic launch failure' }
    [IO.File]::WriteAllText((Join-Path $cfg 'standalone.txt'), 'saved by fake game')
    $result = [pscustomobject]@{Id = 123; HasExited = -not WAIT_FAILURE; ExitCode = 0; WaitCalls = 0}
    $result | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {
        param([int]$Milliseconds)
        if ($Milliseconds -le 0) { throw 'unbounded wait is forbidden by this test' }
        ++$this.WaitCalls
        if ($Milliseconds -eq 250) {
            if (WAIT_FAILURE) { throw 'synthetic wait interruption' }
            # Exercise repeated bounded polling, not only an already-exited process.
            return $this.WaitCalls -gt 1
        }
        $this.HasExited = $true
        return $true
    }
    $result | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value {
        [IO.File]::WriteAllText((Join-Path BUILD 'close-requested.txt'), 'closed')
        return $true
    }
    $result | Add-Member -MemberType ScriptMethod -Name Dispose -Value {}
    return $result
}
""".replace("OBSERVE", observe).replace("FAIL_START", "$true" if fail_start else "$false")
        mock = mock.replace("LEFTOVER", "$true" if leftover else "$false").replace("RUNNING", "$true" if running else "$false")
        mock = mock.replace("WAIT_FAILURE", "$true" if wait_failure else "$false").replace("BUILD", quote(self.build))
        command = mock + "\n& " + quote(SCRIPT) + " -Configuration Debug -BuildDirectory " + quote(self.build)
        command += " " + " ".join(args)
        return subprocess.run(
            [POWERSHELL, "-NoProfile", "-NonInteractive", "-Command", command],
            capture_output=True, text=True, timeout=20,
        )

    def assert_restored(self):
        self.assertEqual((self.cfg / "standalone.txt").read_bytes(), self.original)
        self.assertEqual((self.cfg / "robots.dat").read_bytes(), b"original packed bytes")
        self.assertFalse((self.cfg / ".native-outcome-backup").exists())

    def test_inspect_neither_launches_nor_writes(self):
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Inspection only", result.stdout)
        self.assertIn(hashlib.sha256((self.game / "MatrixGame.exe").read_bytes()).hexdigest().upper(), result.stdout)
        self.assertFalse((self.build / "observed.json").exists())
        self.assert_restored()

    def test_text_launch_restores_original_bytes_and_packed_copy(self):
        result = self.run_helper("-Launch", "-RandomGenerator LegacyCRT")
        self.assertEqual(result.returncode, 0, result.stderr)
        observed = json.loads((self.build / "observed.json").read_text(encoding="utf-8-sig"))
        self.assertFalse(observed["Packed"])
        self.assertTrue(observed["ManifestBeforePacked"])
        self.assertIn("RandomGenerator=LegacyCRT", observed["Settings"])
        self.assertIn("WindowMode=Windowed", observed["Settings"])
        self.assertIn("Map=Matrix\\Map\\desert1_3e.cmap", observed["Settings"])
        self.assertIn("Exit code: 0", result.stdout)
        self.assert_restored()

    def test_packed_launch_keeps_packed_data(self):
        result = self.run_helper("-Launch", "-ConfigurationMode Packed")
        self.assertEqual(result.returncode, 0, result.stderr)
        observed = json.loads((self.build / "observed.json").read_text(encoding="utf-8-sig"))
        self.assertTrue(observed["Packed"])
        self.assert_restored()

    def test_absent_settings_remain_absent(self):
        (self.cfg / "standalone.txt").unlink()
        result = self.run_helper("-Launch")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.cfg / "standalone.txt").exists())
        self.assertFalse((self.cfg / ".native-outcome-backup").exists())

    def test_launch_failure_still_restores(self):
        result = self.run_helper("-Launch", fail_start=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic launch failure", result.stderr)
        self.assert_restored()

    def test_wait_failure_closes_only_mocked_process_and_restores(self):
        result = self.run_helper("-Launch", wait_failure=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic wait interruption", result.stderr)
        self.assertEqual((self.build / "close-requested.txt").read_text(), "closed")
        self.assert_restored()

    def test_msvc_multi_config_cache_and_output_layout(self):
        # tools/build.ps1 supplies this untyped entry to the VS multi-config generator.
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:UNINITIALIZED=Debug\n"
            "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;MinSizeRel;RelWithDebInfo\n"
            "MATRIXGAME_BUILD_DLL:BOOL=OFF\nMATRIXGAME_CHEATS:BOOL=OFF\n", encoding="utf-8",
        )
        original_game = self.game
        original_game.rename(self.build / "staged-game")
        self.game = original_game / "Debug"
        original_game.mkdir()
        (self.build / "staged-game").rename(self.game)
        self.cfg = self.game / "CFG"
        result = self.run_helper("-Compiler MSVC", "-Launch")
        self.assertEqual(result.returncode, 0, result.stderr)
        observed = json.loads((self.build / "observed.json").read_text(encoding="utf-8-sig"))
        self.assertEqual(Path(observed["Executable"]), self.game / "MatrixGame.exe")
        self.assert_restored()
        # The accepted cache type must not relax configuration identity.
        cache = self.build / "CMakeCache.txt"
        cache.write_text(cache.read_text().replace("UNINITIALIZED=Debug", "UNINITIALIZED=Release"))
        result = self.run_helper("-Compiler MSVC", "-Launch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Build configuration mismatch", result.stderr)
        self.assert_restored()

    def test_cheats_mismatch_rejected_before_changes(self):
        result = self.run_helper("-Launch", "-Cheats On")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Build configuration mismatch", result.stderr)
        self.assertFalse((self.build / "observed.json").exists())
        self.assert_restored()

    def test_suffix_alias_rejected_before_changes(self):
        (self.cfg / "robots.dat.backup").write_bytes(b"would match resource search")
        result = self.run_helper("-Launch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("aliases would defeat text fallback", result.stderr)
        self.assert_restored()

    def test_abandoned_backup_requires_explicit_restore(self):
        backup = self.cfg / ".native-outcome-backup"
        backup.mkdir()
        (backup / "standalone.txt").write_bytes(self.original)
        (backup / "robots.dat").write_bytes(b"original packed bytes")
        (backup / "state.json").write_text(json.dumps({"Version": 1, "SettingsPresent": True, "PackedRemoved": True}))
        (self.cfg / "standalone.txt").write_text("interrupted temporary options")
        (self.cfg / "robots.dat").unlink()
        result = self.run_helper("-Launch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("previous backup remains", result.stderr)
        self.assertEqual((self.cfg / "standalone.txt").read_text(), "interrupted temporary options")
        result = self.run_helper("-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_restored()

    def test_leftover_process_retains_backup_until_explicit_recovery(self):
        result = self.run_helper("-Launch", leftover=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("backup retained", result.stdout)
        self.assertTrue((self.cfg / ".native-outcome-backup").exists())
        (self.cfg / "standalone.txt").write_text("independently edited after interruption")
        result = self.run_helper()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.cfg / "standalone.txt").read_text(), "independently edited after interruption")
        result = self.run_helper("-Restore", running=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Close MatrixGame before restoring", result.stderr)
        self.assertEqual((self.cfg / "standalone.txt").read_text(), "independently edited after interruption")
        result = self.run_helper("-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_restored()

    def test_x64_binary_rejected_before_changes(self):
        path = self.game / "MatrixGame.exe"
        pe = bytearray(path.read_bytes())
        struct.pack_into("<H", pe, 68, 0x8664)
        path.write_bytes(pe)
        result = self.run_helper("-Launch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected a Windows x86 executable", result.stderr)
        self.assert_restored()

    def test_incomplete_packed_backup_refuses_before_settings_restore(self):
        backup = self.cfg / ".native-outcome-backup"
        backup.mkdir()
        (backup / "standalone.txt").write_bytes(self.original)
        (backup / "state.json").write_text(json.dumps({"Version": 1, "SettingsPresent": True, "PackedRemoved": True}))
        current = b"independent settings must survive incomplete recovery"
        (self.cfg / "standalone.txt").write_bytes(current)
        result = self.run_helper("-Restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Incomplete backup: robots.dat", result.stderr)
        self.assertEqual((self.cfg / "standalone.txt").read_bytes(), current)
        self.assertTrue((backup / "standalone.txt").exists())
        self.assertTrue((backup / "state.json").exists())

    def test_restore_retains_and_reports_unexpected_backup_file(self):
        self.assertEqual(self.run_helper("-Launch", leftover=True).returncode, 0)
        backup = self.cfg / ".native-outcome-backup"
        extra = backup / "unexpected.bin"
        extra.write_bytes(b"preserve this independent file")
        result = self.run_helper("-Restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unexpected files remain", result.stderr)
        self.assertEqual(extra.read_bytes(), b"preserve this independent file")
        self.assertEqual((self.cfg / "standalone.txt").read_bytes(), self.original)
        self.assertEqual((self.cfg / "robots.dat").read_bytes(), b"original packed bytes")


if __name__ == "__main__":
    unittest.main()
