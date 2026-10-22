# MatrixGame - licensed under GPLv2 or any later version.
"""Bounded fault staging on synthetic copies; no native process is started."""
import json
import os
import hashlib
import struct
import subprocess
import unittest

import test_playtest_outcomes as preparation


SCRIPT = preparation.ROOT / "tools" / "set-outcome-fault.ps1"


@unittest.skipUnless(preparation.POWERSHELL, "Windows PowerShell is required")
class OutcomeFault(unittest.TestCase):
    def setUp(self):
        preparation.OutcomePreparation.setUp(self)
        self.source = self.build / "owned-map.cmap"
        self.original_map = struct.pack("<III", 0x47525453, 0, 0)
        self.source.write_bytes(self.original_map)
        self.target = self.game / "Matrix" / "Map" / "native-outcome-alternate.cmap"
        self.target.parent.mkdir(parents=True)
        self.target.write_bytes(self.original_map)
        self.backup = self.cfg / ".native-outcome-backup"
        self.backup.mkdir()
        (self.backup / "standalone.txt").write_bytes(self.original)
        (self.backup / "robots.dat").write_bytes(b"original packed bytes")
        (self.cfg / "robots.dat").unlink()
        (self.cfg / "standalone.txt").write_bytes(b"temporary settings")
        self.state = {
            "Version": 3, "SettingsPresent": True, "PackedRemoved": True,
            "AlternateMapStaged": True, "MapDirectoryCreated": True,
            "MatrixDirectoryCreated": True, "InterfaceRemoved": False,
        }
        self.save_state()

    def save_state(self):
        (self.backup / "state.json").write_text(json.dumps(self.state))

    def run_fault(self, fault="RestartHeader", apply=True, source=None, processes=1, wrong_path=False, compiler="MinGW", fail_remove=False):
        path = self.game / ("Other.exe" if wrong_path else "MatrixGame.exe")
        mock = """
Get-Date | Out-Null
Get-Item . | Out-Null
Remove-Item Function:\\Get-FileHash -ErrorAction SilentlyContinue
$env:PSModulePath = 'nonexistent-core-only-modules'
$PSModuleAutoLoadingPreference = 'None'
function Remove-Item {
    [CmdletBinding()] param([string]$LiteralPath)
    if ((Split-Path -Leaf $LiteralPath) -eq 'iface.txt') {
        $cfg = Split-Path -Parent (Split-Path -Parent $LiteralPath)
        $backup = Join-Path $cfg '.native-outcome-backup'
        $state = Get-Content -LiteralPath (Join-Path $backup 'state.json') -Raw | ConvertFrom-Json
        $observation = @{ InterfaceRemoved = $state.InterfaceRemoved; BackupPresent = Test-Path -LiteralPath (Join-Path $backup 'iface.txt') }
        $observation | ConvertTo-Json | Set-Content -LiteralPath OBSERVE -Encoding UTF8
        if (FAIL_REMOVE) { throw 'synthetic interface removal failure' }
    }
    Microsoft.PowerShell.Management\\Remove-Item -LiteralPath $LiteralPath
}
function Get-Process {
    [CmdletBinding()] param([string]$Name)
    for ($i = 0; $i -lt COUNT; ++$i) {
        $item = [pscustomobject]@{Path = EXE}
        $item | Add-Member -MemberType ScriptMethod -Name Dispose -Value {}
        $item
    }
}
""".replace("COUNT", str(processes)).replace("EXE", preparation.quote(path))
        mock = mock.replace("OBSERVE", preparation.quote(self.build / "fault-observation.json"))
        mock = mock.replace("FAIL_REMOVE", "$true" if fail_remove else "$false")
        command = mock + "\n& " + preparation.quote(SCRIPT)
        command += " -Configuration Debug -BuildDirectory " + preparation.quote(self.build)
        command += " -Compiler " + compiler
        command += " -SourceMapFile " + preparation.quote(source or self.source) + " -Fault " + fault
        if apply:
            command += " -Apply"
        return subprocess.run(
            [preparation.POWERSHELL, "-NoProfile", "-NonInteractive", "-Command", command],
            capture_output=True, text=True, timeout=20,
        )

    def restore(self):
        result = preparation.OutcomePreparation.run_helper(self, "-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        preparation.OutcomePreparation.assert_restored(self)
        self.assertFalse(self.target.exists())
        self.assertEqual(self.source.read_bytes(), self.original_map)

    def test_inspection_changes_neither_source_nor_copy(self):
        result = self.run_fault(apply=False, processes=0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Inspection only", result.stdout)
        self.assertIn(hashlib.sha256(self.source.read_bytes()).hexdigest().upper(), result.stdout)
        self.assertFalse((self.backup / "restart-header.tmp").exists())
        self.assertFalse((self.backup / "iface.txt").exists())
        self.assertEqual(self.target.read_bytes(), self.original_map)
        self.assertEqual(self.source.read_bytes(), self.original_map)

    def test_bad_header_changes_exactly_the_copy_tag_and_restores(self):
        result = self.run_fault()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.target.read_bytes(), b"\0" * 4 + self.original_map[4:])
        self.assertEqual(self.source.read_bytes(), self.original_map)
        self.restore()

    def test_source_cannot_be_the_copy_itself(self):
        result = self.run_fault(source=self.target)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside the disposable target", result.stderr)
        self.assertEqual(self.target.read_bytes(), self.original_map)

    def test_source_mismatch_refuses_changes(self):
        self.source.write_bytes(self.original_map + b"different")
        result = self.run_fault()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("differs from SourceMapFile", result.stderr)
        self.assertEqual(self.target.read_bytes(), self.original_map)

    def test_already_malformed_copy_is_rejected(self):
        self.source.write_bytes(b"wrong header")
        self.target.write_bytes(b"wrong header")
        result = self.run_fault()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("complete storage header", result.stderr)
        self.assertEqual(self.target.read_bytes(), b"wrong header")

    def test_requires_one_matching_live_process(self):
        for count, wrong in ((0, False), (2, False), (1, True)):
            with self.subTest(count=count, wrong=wrong):
                result = self.run_fault(processes=count, wrong_path=wrong)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Exactly one MatrixGame", result.stderr)
                self.assertEqual(self.target.read_bytes(), self.original_map)

    def test_interface_failure_restores_original_bytes(self):
        interface = self.cfg / "robots" / "iface.txt"
        original = b"\xff\xfe" + "original interface\r\n".encode("utf-16-le")
        interface.write_bytes(original)
        result = self.run_fault("MissingInterface")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(interface.exists())
        observation = json.loads((self.build / "fault-observation.json").read_text(encoding="utf-8-sig"))
        self.assertTrue(observation["InterfaceRemoved"])
        self.assertTrue(observation["BackupPresent"])
        self.assertEqual((self.backup / "iface.txt").read_bytes(), original)
        self.assertTrue(json.loads((self.backup / "state.json").read_text(encoding="utf-8-sig"))["InterfaceRemoved"])
        self.restore()
        self.assertEqual(interface.read_bytes(), original)

    def test_missing_interface_stays_absent_when_no_fault_can_be_applied(self):
        interface = self.cfg / "robots" / "iface.txt"
        interface.unlink()
        result = self.run_fault("MissingInterface")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already absent", result.stderr)
        self.restore()
        self.assertFalse(interface.exists())

    def test_packed_and_suffix_configuration_refuse_missing_interface(self):
        for name in ("robots.dat", "robots.dat.backup"):
            with self.subTest(name=name):
                path = self.cfg / name
                path.write_bytes(b"would suppress text interface read")
                result = self.run_fault("MissingInterface")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires the Text profile", result.stderr)
                self.assertTrue((self.cfg / "robots" / "iface.txt").exists())
                path.unlink()

    def test_incomplete_interface_backup_prevents_partial_restoration(self):
        self.state["InterfaceRemoved"] = True
        self.save_state()
        result = preparation.OutcomePreparation.run_helper(self, "-Restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Incomplete backup: iface.txt", result.stderr)
        self.assertEqual((self.cfg / "standalone.txt").read_bytes(), b"temporary settings")
        self.assertTrue(self.backup.exists())

    def test_repeat_fault_does_not_overwrite_its_backup(self):
        first = self.run_fault("MissingInterface")
        self.assertEqual(first.returncode, 0, first.stderr)
        original = (self.backup / "iface.txt").read_bytes()
        result = self.run_fault("MissingInterface")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no fault already applied", result.stderr)
        self.assertEqual((self.backup / "iface.txt").read_bytes(), original)
        self.restore()

    def test_missing_interface_inspection_preserves_exact_files(self):
        before = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
        result = self.run_fault("MissingInterface", apply=False, processes=0)
        self.assertEqual(result.returncode, 0, result.stderr)
        after = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
        self.assertEqual(after, before)

    def test_hard_link_to_source_is_replaced_without_mutating_source(self):
        self.target.unlink()
        os.link(self.source, self.target)
        self.assertTrue(os.path.samefile(self.source, self.target))
        result = self.run_fault()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_bytes(), self.original_map)
        self.assertEqual(self.target.read_bytes(), b"\0" * 4 + self.original_map[4:])
        self.assertFalse(os.path.samefile(self.source, self.target))
        self.assertFalse((self.backup / "restart-header.tmp").exists())
        self.restore()

    def test_preexisting_fault_temporary_is_preserved(self):
        temporary = self.backup / "restart-header.tmp"
        temporary.write_bytes(b"independent retained file")
        result = self.run_fault()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(temporary.read_bytes(), b"independent retained file")
        self.assertEqual(self.target.read_bytes(), self.original_map)
        self.assertEqual(self.source.read_bytes(), self.original_map)

    def test_incomplete_playtest_backup_refuses_before_fault(self):
        for name in ("standalone.txt", "robots.dat"):
            with self.subTest(name=name):
                path = self.backup / name
                saved = path.read_bytes()
                path.unlink()
                try:
                    result = self.run_fault("MissingInterface")
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Incomplete playtest backup: " + name, result.stderr)
                    self.assertTrue((self.cfg / "robots" / "iface.txt").exists())
                    self.assertFalse((self.backup / "iface.txt").exists())
                finally:
                    path.write_bytes(saved)

    def test_invalid_manifest_fields_refuse_before_fault(self):
        for name in ("SettingsPresent", "PackedRemoved", "AlternateMapStaged", "MapDirectoryCreated",
                     "MatrixDirectoryCreated", "InterfaceRemoved"):
            with self.subTest(name=name):
                original = self.state[name]
                self.state[name] = "false"
                self.save_state()
                try:
                    result = self.run_fault()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("current alternate-map playtest backup", result.stderr)
                    self.assertEqual(self.target.read_bytes(), self.original_map)
                    self.assertEqual(self.source.read_bytes(), self.original_map)
                finally:
                    self.state[name] = original
                    self.save_state()

    def test_msvc_layout_fault_and_interface_recovery(self):
        staged = self.build / "multi-config-stage"
        self.game.rename(staged)
        self.game.mkdir()
        staged.rename(self.game / "Debug")
        self.game = self.game / "Debug"
        self.cfg = self.game / "CFG"
        self.backup = self.cfg / ".native-outcome-backup"
        self.target = self.game / "Matrix" / "Map" / "native-outcome-alternate.cmap"
        interface = self.cfg / "robots" / "iface.txt"
        original = interface.read_bytes()
        result = self.run_fault("MissingInterface", compiler="MSVC")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(interface.exists())
        result = preparation.OutcomePreparation.run_helper(self, "-Compiler MSVC", "-Restore")
        self.assertEqual(result.returncode, 0, result.stderr)
        preparation.OutcomePreparation.assert_restored(self)
        self.assertEqual(interface.read_bytes(), original)
        self.assertFalse(self.target.exists())

    def test_prior_v2_record_still_restores(self):
        self.state["Version"] = 2
        del self.state["InterfaceRemoved"]
        self.save_state()
        self.restore()

    def test_unclaimed_interface_backup_is_preserved(self):
        unexpected = self.backup / "iface.txt"
        unexpected.write_bytes(b"independent unclaimed interface backup")
        result = preparation.OutcomePreparation.run_helper(self, "-Restore")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unexpected files remain", result.stderr)
        self.assertEqual(unexpected.read_bytes(), b"independent unclaimed interface backup")

    def test_unclaimed_interface_backup_refuses_fault_without_changes(self):
        unexpected = self.backup / "iface.txt"
        unexpected.write_bytes(b"independent unclaimed interface backup")
        before = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
        result = self.run_fault("MissingInterface")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("interface backup already exists", result.stderr)
        after = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
        self.assertEqual(after, before)

    def test_incomplete_v3_record_refuses_before_restoration(self):
        for name in ("Version", "InterfaceRemoved"):
            with self.subTest(name=name):
                original = self.state.pop(name)
                self.save_state()
                try:
                    before = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
                    result = preparation.OutcomePreparation.run_helper(self, "-Restore")
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Invalid restoration record", result.stderr)
                    after = {p.relative_to(self.build): p.read_bytes() for p in self.build.rglob("*") if p.is_file()}
                    self.assertEqual(after, before)
                finally:
                    self.state[name] = original
                    self.save_state()

    def test_failed_header_replacement_removes_only_its_temporary(self):
        # A normal Python read handle permits hashing but denies Windows replacement.
        # The prepared fault must fail before replacing this locked directory entry.
        with self.target.open("rb"):
            result = self.run_fault()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.target.read_bytes(), self.original_map)
        self.assertEqual(self.source.read_bytes(), self.original_map)
        self.assertFalse((self.backup / "restart-header.tmp").exists())
        self.assertTrue((self.backup / "state.json").is_file())
        self.restore()

    def test_interface_removal_failure_keeps_complete_recovery(self):
        interface = self.cfg / "robots" / "iface.txt"
        original = interface.read_bytes()
        result = self.run_fault("MissingInterface", fail_remove=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("synthetic interface removal failure", result.stderr)
        self.assertEqual(interface.read_bytes(), original)
        observation = json.loads((self.build / "fault-observation.json").read_text(encoding="utf-8-sig"))
        self.assertTrue(observation["InterfaceRemoved"])
        self.assertTrue(observation["BackupPresent"])
        self.restore()
        self.assertEqual(interface.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
