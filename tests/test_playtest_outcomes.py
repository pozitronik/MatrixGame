# MatrixGame - licensed under GPLv2 or any later version.
"""Exercise preparation/restoration with synthetic files and a mocked process only."""
import json
import base64
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

    def run_helper(self, *args, fail_start=False, leftover=False, running=False, wait_failure=False, child=False, child_exit=0, foreign_child=False, monitor_failure=False, child_id=124, repeated_child=False, child_live=False, close_timeout=False, kill_timeout=False):
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
    if (HAS_CHILD -and ($global:outcomeCalls -eq 2 -or (REPEATED_CHILD -and $global:outcomeCalls -le 5 -and $global:outcomeCalls -ge 2))) { return $global:outcomeChild }
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
        Alternate = Test-Path -LiteralPath (Join-Path $WorkingDirectory 'Matrix/Map/native-outcome-alternate.cmap')
        AlternateBytes = $(if (Test-Path -LiteralPath (Join-Path $WorkingDirectory 'Matrix/Map/native-outcome-alternate.cmap')) {
            [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $WorkingDirectory 'Matrix/Map/native-outcome-alternate.cmap')))
        })
    }
    $observation | ConvertTo-Json | Set-Content -LiteralPath OBSERVE -Encoding UTF8
    if (FAIL_START) { throw 'synthetic launch failure' }
    [IO.File]::WriteAllText((Join-Path $cfg 'standalone.txt'), 'saved by fake game')
    $result = [pscustomobject]@{Id = 123; HasExited = -not WAIT_FAILURE; ExitCode = 0; WaitCalls = 0; Killed = $false; Handle = 1; StartTime = [datetime]'2026-01-01T00:00:00Z'}
    $result | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {
        param([int]$Milliseconds)
        if ($Milliseconds -le 0) { throw 'unbounded wait is forbidden by this test' }
        [IO.File]::AppendAllText((Join-Path BUILD 'waits.txt'), [string]$Milliseconds + [Environment]::NewLine)
        ++$this.WaitCalls
        if ($Milliseconds -eq 250) {
            if (WAIT_FAILURE) { throw 'synthetic wait interruption' }
            # Exercise repeated bounded polling, not only an already-exited process.
            return $this.WaitCalls -gt 1
        }
        if ((CLOSE_TIMEOUT -and -not $this.Killed) -or KILL_TIMEOUT) { return $false }
        $this.HasExited = $true
        return $true
    }
    $result | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value {
        [IO.File]::WriteAllText((Join-Path BUILD 'close-requested.txt'), 'closed')
        return $true
    }
    $result | Add-Member -MemberType ScriptMethod -Name Kill -Value {
        $this.Killed = $true
        [IO.File]::WriteAllText((Join-Path BUILD 'kill-requested.txt'), 'killed')
    }
    $result | Add-Member -MemberType ScriptMethod -Name Dispose -Value {}
    $global:outcomeChild = [pscustomobject]@{Id = CHILD_ID; HasExited = -not CHILD_LIVE; ExitCode = CHILD_EXIT; Handle = 2; StartTime = [datetime]'2026-01-01T00:00:01Z'; Path = $(if (FOREIGN_CHILD) { 'C:/other/MatrixGame.exe' } else { $FilePath })}
    $global:outcomeChild | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param([int]$Milliseconds); return $true }
    $global:outcomeChild | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value {
        [IO.File]::WriteAllText((Join-Path BUILD 'unrelated-close.txt'), 'must not close')
        return $false
    }
    $global:outcomeChild | Add-Member -MemberType ScriptMethod -Name Kill -Value {
        [IO.File]::WriteAllText((Join-Path BUILD 'unrelated-kill.txt'), 'must not kill')
    }
    $global:outcomeChild | Add-Member -MemberType ScriptMethod -Name Dispose -Value {
        [IO.File]::WriteAllText((Join-Path BUILD 'unrelated-disposed.txt'), 'disposed read-only')
    }
    return $result
}
function Start-Sleep { param([int]$Milliseconds); if (MONITOR_FAILURE) { throw 'synthetic monitor interruption' } }
""".replace("OBSERVE", observe).replace("FAIL_START", "$true" if fail_start else "$false")
        mock = mock.replace("LEFTOVER", "$true" if leftover else "$false").replace("RUNNING", "$true" if running else "$false")
        mock = mock.replace("WAIT_FAILURE", "$true" if wait_failure else "$false").replace("BUILD", quote(self.build))
        mock = mock.replace("HAS_CHILD", "$true" if child else "$false").replace("CHILD_EXIT", str(child_exit))
        mock = mock.replace("FOREIGN_CHILD", "$true" if foreign_child else "$false")
        mock = mock.replace("MONITOR_FAILURE", "$true" if monitor_failure else "$false")
        mock = mock.replace("CHILD_ID", str(child_id)).replace("REPEATED_CHILD", "$true" if repeated_child else "$false")
        mock = mock.replace("CHILD_LIVE", "$true" if child_live else "$false")
        mock = mock.replace("CLOSE_TIMEOUT", "$true" if close_timeout else "$false")
        mock = mock.replace("KILL_TIMEOUT", "$true" if kill_timeout else "$false")
        command = mock + "\ntry { & " + quote(SCRIPT) + " -Configuration Debug -BuildDirectory " + quote(self.build)
        command += " " + " ".join(args)
        # Keep diagnostics exact: PowerShell's error formatter wraps paths and even message words
        # according to host width, which otherwise makes assertions depend on the checkout path.
        command += "\n} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }"
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
        shadow = self.game / "DATA" / "maps" / "DESERT1_3E.CMAP"
        shadow.parent.mkdir()
        shadow.write_bytes(b"independent MSVC map")
        result = self.run_helper("-Compiler MSVC", "-Launch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("higher-priority map shadows", result.stderr)
        self.assertEqual(shadow.read_bytes(), b"independent MSVC map")
        self.assert_restored()
        shadow.unlink()
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
    def test_observed_relaunch_reports_both_exits(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Process ID: 123; exit code: 0", result.stdout)
        self.assertIn("Process ID: 124; exit code: 0", result.stdout)
        self.assertIn("Observed processes: 2; expected: 2", result.stdout)
        self.assert_restored()

    def test_parent_exit_cannot_substitute_for_missing_child(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Process evidence is incomplete", result.stderr)
        self.assertIn("Observed processes: 1; expected: 2", result.stdout)
        self.assert_restored()

    def test_child_failure_is_not_a_passing_relaunch(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True, child_exit=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Process ID: 124; exit code: 1", result.stdout)
        self.assert_restored()

    def test_alternate_map_staging_removes_only_its_output_copy(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        result = self.run_helper("-Launch", "-AlternateMapFile " + quote(source))
        self.assertEqual(result.returncode, 0, result.stderr)
        observed = json.loads((self.build / "observed.json").read_text(encoding="utf-8-sig"))
        self.assertTrue(observed["Alternate"])
        self.assertEqual(base64.b64decode(observed["AlternateBytes"]), source.read_bytes())
        self.assertIn(hashlib.sha256(source.read_bytes()).hexdigest().upper(), result.stdout)
        self.assertEqual(source.read_bytes(), b"synthetic alternative")
        self.assertFalse((self.game / "Matrix").exists())
        self.assert_restored()

    def test_existing_alternate_map_is_never_overwritten(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        target = self.game / "Matrix" / "Map" / "native-outcome-alternate.cmap"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"unrelated existing map")
        result = self.run_helper("-Launch", "-AlternateMapFile " + quote(source))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("reserved alternate-map output already exists", result.stderr)
        self.assertEqual(target.read_bytes(), b"unrelated existing map")
        self.assert_restored()

    def test_first_map_shadow_is_rejected_before_inspection_or_launch(self):
        for folder in ("maps", "DATA/maps"):
            for launch in (False, True):
                with self.subTest(folder=folder, launch=launch):
                    shadow = self.game / folder / "DESERT1_3E.CMAP"
                    shadow.parent.mkdir(parents=True, exist_ok=True)
                    shadow.write_bytes(b"independent map must survive")
                    try:
                        result = self.run_helper(*(["-Launch"] if launch else []))
                        self.assertNotEqual(result.returncode, 0)
                        prefix = "A higher-priority map shadows a required selector fixture: "
                        self.assertTrue(result.stderr.startswith(prefix), result.stderr)
                        reported = result.stderr[len(prefix):].split(". Move that output copy aside manually", 1)[0]
                        # PowerShell expands Windows short names in TEMP; compare the actual file,
                        # not the spelling chosen by Python's temporary-directory implementation.
                        self.assertTrue(shadow.samefile(reported), result.stderr)
                        self.assertEqual(shadow.read_bytes(), b"independent map must survive")
                        self.assertFalse((self.build / "observed.json").exists())
                        self.assert_restored()
                    finally:
                        shadow.unlink()

    def test_alternate_map_shadow_is_rejected_before_inspection_or_staging(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        for folder in ("maps", "DATA/maps"):
            for launch in (False, True):
                with self.subTest(folder=folder, launch=launch):
                    shadow = self.game / folder / "NATIVE-OUTCOME-ALTERNATE.CMAP"
                    shadow.parent.mkdir(parents=True, exist_ok=True)
                    shadow.write_bytes(b"independent alternate must survive")
                    try:
                        args = ["-AlternateMapFile " + quote(source)]
                        if launch:
                            args.append("-Launch")
                        result = self.run_helper(*args)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("higher-priority map shadows", result.stderr)
                        self.assertEqual(shadow.read_bytes(), b"independent alternate must survive")
                        self.assertEqual(source.read_bytes(), b"synthetic alternative")
                        self.assertFalse((self.game / "Matrix").exists())
                        self.assertFalse((self.build / "observed.json").exists())
                        self.assert_restored()
                    finally:
                        shadow.unlink()

    def test_unrelated_and_nested_maps_do_not_block_staging(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        preserved = []
        for folder in ("maps", "DATA/maps"):
            for name in ("Unrelated.cmap", "Nested/desert1_3e.cmap", "Nested/native-outcome-alternate.cmap"):
                path = self.game / folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"independent map")
                preserved.append(path)
        result = self.run_helper("-Launch", "-AlternateMapFile " + quote(source))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads((self.build / "observed.json").read_text(encoding="utf-8-sig"))["Alternate"])
        for path in preserved:
            self.assertEqual(path.read_bytes(), b"independent map")
        self.assertFalse((self.game / "Matrix").exists())
        self.assert_restored()

    def test_restore_is_not_blocked_by_a_new_map_override(self):
        self.assertEqual(self.run_helper("-Launch", leftover=True).returncode, 0)
        shadow = self.game / "maps" / "desert1_3e.cmap"
        shadow.parent.mkdir()
        shadow.write_bytes(b"map installed after interruption")
        result = self.run_helper("-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(shadow.read_bytes(), b"map installed after interruption")
        self.assert_restored()

    def test_unused_alternate_override_does_not_block_the_first_map(self):
        shadow = self.game / "maps" / "native-outcome-alternate.cmap"
        shadow.parent.mkdir()
        shadow.write_bytes(b"independent map not used by this run")
        result = self.run_helper("-Launch")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(shadow.read_bytes(), b"independent map not used by this run")
        self.assert_restored()

    def test_alternate_launch_failure_keeps_existing_map_directory(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        map_root = self.game / "Matrix" / "Map"
        map_root.mkdir(parents=True)
        unrelated = map_root / "unrelated.cmap"
        unrelated.write_bytes(b"existing user map")
        result = self.run_helper("-Launch", "-AlternateMapFile " + quote(source), fail_start=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic launch failure", result.stderr)
        self.assertTrue(map_root.is_dir())
        self.assertEqual(unrelated.read_bytes(), b"existing user map")
        self.assertFalse((map_root / "native-outcome-alternate.cmap").exists())
        self.assertEqual(source.read_bytes(), b"synthetic alternative")
        self.assert_restored()


    def test_alternate_inspection_remains_read_only(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        result = self.run_helper("-AlternateMapFile " + quote(source), "-WatchRelaunches", "-ExpectedProcesses 2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(hashlib.sha256(source.read_bytes()).hexdigest().upper(), result.stdout)
        self.assertFalse((self.game / "Matrix").exists())
        self.assertFalse((self.build / "observed.json").exists())
        self.assert_restored()

    def test_process_count_requires_monitor(self):
        result = self.run_helper("-Launch", "-ExpectedProcesses 2")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires -WatchRelaunches", result.stderr)
        self.assertFalse((self.build / "observed.json").exists())
        self.assert_restored()

    def test_process_from_another_executable_is_not_a_relaunch(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True, foreign_child=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Observed processes: 1; expected: 2", result.stdout)
        self.assertNotIn("Observed process ID: 124", result.stdout)
        self.assert_restored()

    def test_extra_observed_process_is_not_a_passing_control(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", child=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Observed processes: 2; expected: 1", result.stdout)
        self.assert_restored()

    def test_missing_observed_exit_is_not_a_passing_relaunch(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True, child_exit="$null")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Process evidence is incomplete", result.stderr)
        self.assert_restored()

    def test_monitor_interruption_closes_tracked_process_and_restores(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", wait_failure=True, monitor_failure=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic monitor interruption", result.stderr)
        self.assertEqual((self.build / "close-requested.txt").read_text(), "closed")
        self.assert_restored()

    def test_abandoned_alternate_map_restores_from_record(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        result = self.run_helper("-Launch", "-AlternateMapFile " + quote(source), leftover=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        target = self.game / "Matrix" / "Map" / "native-outcome-alternate.cmap"
        self.assertEqual(target.read_bytes(), source.read_bytes())
        result = self.run_helper("-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.game / "Matrix").exists())
        self.assertEqual(source.read_bytes(), b"synthetic alternative")
        self.assert_restored()

    def test_invalid_alternate_record_refuses_before_mutation(self):
        source = self.build / "owned-map.cmap"
        source.write_bytes(b"synthetic alternative")
        self.assertEqual(self.run_helper("-Launch", "-AlternateMapFile " + quote(source), leftover=True).returncode, 0)
        manifest = self.cfg / ".native-outcome-backup" / "state.json"
        record = json.loads(manifest.read_text(encoding="utf-8-sig"))
        record["AlternateMapStaged"] = "true"
        manifest.write_text(json.dumps(record), encoding="utf-8")
        current = (self.cfg / "standalone.txt").read_bytes()
        result = self.run_helper("-Restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid restoration record", result.stderr)
        self.assertEqual((self.cfg / "standalone.txt").read_bytes(), current)
        self.assertTrue((self.game / "Matrix" / "Map" / "native-outcome-alternate.cmap").exists())

    def test_repeated_process_observation_counts_one_lifetime(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True, repeated_child=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("Observed process ID: 124"), 1)
        self.assertEqual(result.stdout.count("Process ID: 124; exit code: 0"), 1)
        self.assert_restored()

    def test_reused_pid_with_new_start_time_is_a_distinct_lifetime(self):
        result = self.run_helper("-Launch", "-WatchRelaunches", "-ExpectedProcesses 2", child=True, child_id=123)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("Process ID: 123; exit code: 0"), 2)
        self.assertIn("Observed processes: 2; expected: 2", result.stdout)
        self.assert_restored()

    def test_interruption_never_terminates_unowned_same_path_lifetime(self):
        # A polled same-path process with even the same PID is not the exact
        # process object returned by Start-Process. Polling grants no ownership.
        result = self.run_helper("-Launch", "-WatchRelaunches", child=True, child_id=123,
                                 child_live=True, wait_failure=True, monitor_failure=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic monitor interruption", result.stderr)
        self.assertEqual((self.build / "close-requested.txt").read_text(), "closed")
        self.assertFalse((self.build / "unrelated-close.txt").exists())
        self.assertFalse((self.build / "unrelated-kill.txt").exists())
        self.assertEqual((self.build / "unrelated-disposed.txt").read_text(), "disposed read-only")
        self.assertIn("backup retained", result.stdout)
        self.assertTrue((self.cfg / ".native-outcome-backup").exists())
        self.assertFalse((self.cfg / "robots.dat").exists())
        self.assertEqual(self.run_helper("-Restore").returncode, 0)
        self.assert_restored()

    def test_forced_owned_closure_uses_a_bounded_post_kill_wait(self):
        result = self.run_helper("-Launch", wait_failure=True, close_timeout=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("forced closure", result.stdout)
        self.assertEqual((self.build / "kill-requested.txt").read_text(), "killed")
        self.assertEqual((self.build / "waits.txt").read_text().splitlines(), ["250", "5000", "5000"])
        self.assert_restored()

    def test_owned_post_kill_timeout_retains_backup(self):
        result = self.run_helper("-Launch", wait_failure=True, close_timeout=True, kill_timeout=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not exit after forced closure", result.stdout)
        self.assertEqual((self.build / "waits.txt").read_text().splitlines(), ["250", "5000", "5000"])
        self.assertTrue((self.cfg / ".native-outcome-backup").exists())
        self.assertFalse((self.cfg / "robots.dat").exists())
        self.assertEqual(self.run_helper("-Restore").returncode, 0)
        self.assert_restored()


if __name__ == "__main__":
    unittest.main()
