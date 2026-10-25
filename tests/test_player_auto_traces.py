"""CLI contracts for the post-engine auto-order trace check."""
import contextlib
import io
import itertools
import json
from pathlib import Path
import tempfile
import unittest

import check_player_auto_traces as CHECK
from test_battle_traces import text, trace


class AutoTraceContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.names = [name for pair in CHECK.expected_pairs() for name in pair]
        self.records = trace()
        self.records[0]["fixture"] = "player-auto-order-continuation-v1"
        self.records[-2]["state"]["robots"][0].update(order=0, y_bits=0)
        self.records[-1].update({field: 0 for field in CHECK.MILESTONES})
        self.records[-1]["terminal_x_bits"] = self.records[-2]["state"]["robots"][0]["x_bits"]
        for name in self.names:
            _, _, scene, action, rng, step, _ = name.removesuffix(".jsonl").split("-")
            records = json.loads(json.dumps(self.records))
            records[0].update(auto_order=scene, manual_override=action == "override",
                              rng={"pm": "ParkMiller", "crt": "LegacyCRT"}[rng], step_ms=int(step))
            (self.directory / name).write_text(text(records), encoding="utf-8")
        self.records = [json.loads(line) for line in
                        (self.directory / self.names[0]).read_text(encoding="utf-8").splitlines()]

    def run_cli(self, *extra):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return CHECK.main([str(self.directory), *extra])

    def overwrite(self, records, both=False):
        for name in self.names[:2 if both else 1]:
            (self.directory / name).write_text(text(records), encoding="utf-8")

    def test_complete_inventory_and_matching_reference_pass(self):
        self.assertEqual(72, len(set(self.names)))
        self.assertEqual(0, self.run_cli())
        self.assertEqual(0, self.run_cli("--reference-directory", str(self.directory)))

    def test_missing_trace_fails(self):
        (self.directory / self.names[-1]).unlink()
        self.assertEqual(1, self.run_cli())

    def test_missing_directory_fails(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(1, CHECK.main([str(self.directory / "absent")]))

    def test_obsolete_truncated_and_malformed_tail_fail(self):
        for records in (self.records[:-1], self.records[:-1] + [dict(self.records[-1], type="milestones")],
                        self.records + [{"type": "unexpected"}]):
            with self.subTest(tail=records[-1]):
                self.overwrite(records)
                self.assertEqual(1, self.run_cli())

    def test_missing_milestone_fails(self):
        for field in CHECK.MILESTONES:
            with self.subTest(field=field):
                completion = dict(self.records[-1])
                del completion[field]
                self.overwrite(self.records[:-1] + [completion], both=True)
                self.assertEqual(1, self.run_cli())

    def test_terminal_state_mismatch_fails(self):
        for field in ("terminal_order", "terminal_x_bits", "terminal_y_bits"):
            with self.subTest(field=field):
                completion = dict(self.records[-1], **{field: self.records[-1][field] + 1})
                self.overwrite(self.records[:-1] + [completion], both=True)
                self.assertEqual(1, self.run_cli())

    def test_wrong_file_profile_fails(self):
        for field, value in (("rng", "LegacyCRT"), ("auto_order", "attack"),
                             ("manual_override", False), ("step_ms", 16)):
            with self.subTest(field=field):
                records = [dict(self.records[0], **{field: value}), *self.records[1:]]
                self.overwrite(records, both=True)
                self.assertEqual(1, self.run_cli())

    def test_wrong_fixture_fails_even_when_both_replays_match(self):
        records = [dict(self.records[0], fixture="another-nonempty-fixture"), *self.records[1:]]
        self.overwrite(records, both=True)
        self.assertEqual(1, self.run_cli())

    def test_noninteger_milestones_fail_even_when_both_replays_match(self):
        for field in CHECK.MILESTONES:
            for value in (float(self.records[-1][field]), bool(self.records[-1][field])):
                with self.subTest(field=field, value=value):
                    self.overwrite(self.records[:-1] + [dict(self.records[-1], **{field: value})], both=True)
                    self.assertEqual(1, self.run_cli())

    def test_every_milestone_difference_is_compared(self):
        left = CHECK.validated(self.records)
        for field in CHECK.MILESTONES:
            with self.subTest(field=field):
                changed = self.records[:-1] + [dict(self.records[-1], **{field: self.records[-1][field] + 1})]
                difference = CHECK.TRACES.compare_records(left, CHECK.validated(changed))
                self.assertIn("$." + field, difference)

    def test_milestone_replay_mismatch_fails(self):
        self.overwrite(self.records[:-1] + [dict(self.records[-1], paint_ms=1)])
        self.assertEqual(1, self.run_cli())

    def test_fresh_process_difference_fails(self):
        reference = self.directory / "reference"
        reference.mkdir()
        for name in self.names:
            (reference / name).write_bytes((self.directory / name).read_bytes())
        (reference / self.names[0]).write_text(
            text(self.records[:-1] + [dict(self.records[-1], paint_ms=1)]), encoding="utf-8")
        self.assertEqual(1, self.run_cli("--reference-directory", str(reference)))

    def profile_arguments(self, revision="a" * 40, configuration="Debug", compiler="MinGW", target="EXE", cheats=False):
        return ["--revision", revision, "--configuration", configuration, "--compiler", compiler,
                "--target", target, "--cheats", "true" if cheats else "false"]

    def pin_corpus(self, directory=None, **changes):
        directory = self.directory if directory is None else directory
        for name in self.names:
            path = directory / name
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            identity = dict(revision="a" * 40, dirty=0, configuration="Debug", compiler="MinGW",
                            build_exe=True, cheats=False)
            identity.update(changes)
            records[0].update(identity)
            path.write_text(text(records), encoding="utf-8")
        if directory == self.directory:
            self.records = [json.loads(line) for line in
                            (directory / self.names[0]).read_text(encoding="utf-8").splitlines()]

    def reference_corpus(self):
        directory = self.directory / "reference"
        directory.mkdir()
        for name in self.names:
            (directory / name).write_bytes((self.directory / name).read_bytes())
        return directory

    def test_strict_profile_accepts_supported_flag_combinations(self):
        for configuration, compiler, target, cheats in itertools.product(
                ("Debug", "Release"), ("MinGW", "MSVC"), ("EXE", "DLL"), (False, True)):
            with self.subTest(configuration=configuration, compiler=compiler, target=target, cheats=cheats):
                self.pin_corpus(configuration=configuration, compiler=compiler,
                                build_exe=target == "EXE", cheats=cheats)
                self.assertEqual(0, self.run_cli(*self.profile_arguments(
                    configuration=configuration, compiler=compiler, target=target, cheats=cheats)))

    def test_partial_profile_flags_are_rejected(self):
        arguments = self.profile_arguments()
        for index in range(0, len(arguments), 2):
            for partial in (arguments[index:index + 2], arguments[:index] + arguments[index + 2:]):
                with self.subTest(partial=partial):
                    self.assertEqual(1, self.run_cli(*partial))

    def test_invalid_revision_argument_is_rejected(self):
        for revision in ("", "a" * 39, "a" * 41, "A" * 40, "g" * 40, "-1", "2147483648"):
            with self.subTest(revision=revision):
                self.assertEqual(1, self.run_cli(*self.profile_arguments(revision=revision)))

    def test_correlated_valid_profile_mismatches_need_external_binding(self):
        self.pin_corpus()
        for field, value in (("revision", "b" * 40), ("dirty", 1), ("configuration", "Release"),
                             ("compiler", "MSVC"), ("build_exe", False), ("cheats", True)):
            records = [dict(self.records[0], **{field: value}), *self.records[1:]]
            self.overwrite(records, both=True)
            with self.subTest(field=field):
                self.assertEqual(0, self.run_cli())
                self.assertEqual(1, self.run_cli(*self.profile_arguments()))

    def test_strict_profile_rejects_missing_and_mistyped_fields(self):
        self.pin_corpus()
        values = {
            "revision": [None, False, 0, -1, 0x80000000, "short", "A" * 40, "g" * 40],
            "dirty": [None, False, True, -1, 1, 0x80000000, 0x100000000, 0.0, "0"],
            "configuration": [None, False, 0, "", "debug"],
            "compiler": [None, False, 0, "", "mingw"],
            "build_exe": [None, 0, 1, -1, 0x80000000, 1.0, "true"],
            "cheats": [None, 0, 1, -1, 0x80000000, 0.0, "false"],
        }
        for field, invalid in values.items():
            for value in [*invalid, "missing"]:
                header = dict(self.records[0])
                if value == "missing":
                    del header[field]
                else:
                    header[field] = value
                self.overwrite([header, *self.records[1:]], both=True)
                with self.subTest(field=field, value=value):
                    self.assertEqual(1, self.run_cli(*self.profile_arguments()))

    def test_strict_reference_can_use_a_different_clean_revision(self):
        self.pin_corpus()
        reference = self.reference_corpus()
        self.pin_corpus(reference, revision="b" * 40)
        arguments = [*self.profile_arguments(), "--reference-directory", str(reference)]
        self.assertEqual(0, self.run_cli(*arguments))
        self.assertEqual(0, self.run_cli(*arguments, "--reference-revision", "b" * 40))
        self.assertEqual(1, self.run_cli(*arguments, "--reference-revision", "c" * 40))

    def test_strict_reference_requires_valid_clean_identity_even_when_unpinned(self):
        self.pin_corpus()
        reference = self.reference_corpus()
        arguments = [*self.profile_arguments(), "--reference-directory", str(reference)]
        for field, value in (("revision", None), ("revision", "short"), ("revision", "A" * 40),
                             ("revision", False), ("dirty", 1), ("dirty", False), ("dirty", 0.0),
                             ("dirty", -1), ("dirty", 0x80000000), ("revision", "missing"), ("dirty", "missing")):
            header = dict(self.records[0])
            if value == "missing":
                del header[field]
            else:
                header[field] = value
            for name in self.names[:2]:
                (reference / name).write_text(text([header, *self.records[1:]]), encoding="utf-8")
            with self.subTest(field=field, value=value):
                self.assertEqual(0, self.run_cli("--reference-directory", str(reference)))
                self.assertEqual(1, self.run_cli(*arguments))

    def test_strict_reference_build_profile_must_match_candidate(self):
        self.pin_corpus()
        reference = self.reference_corpus()
        for field, value in (("configuration", "Release"), ("compiler", "MSVC"), ("build_exe", False),
                             ("cheats", True), ("build_exe", 1), ("cheats", 0)):
            header = dict(self.records[0], **{field: value})
            for name in self.names[:2]:
                (reference / name).write_text(text([header, *self.records[1:]]), encoding="utf-8")
            with self.subTest(field=field, value=value):
                self.assertEqual(1, self.run_cli(
                    *self.profile_arguments(), "--reference-directory", str(reference)))

    def test_reference_revision_requires_directory_and_complete_profile(self):
        self.pin_corpus()
        reference = self.reference_corpus()
        self.assertEqual(1, self.run_cli(*self.profile_arguments(), "--reference-revision", "b" * 40))
        self.assertEqual(1, self.run_cli(
            "--reference-directory", str(reference), "--reference-revision", "b" * 40))
        self.assertEqual(1, self.run_cli(
            *self.profile_arguments(), "--reference-directory", str(reference), "--reference-revision", "invalid"))
        with self.assertRaises(SystemExit) as missing:
            self.run_cli(*self.profile_arguments(), "--reference-directory", str(reference), "--reference-revision")
        self.assertEqual(missing.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
