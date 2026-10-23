"""CLI contracts for the post-engine auto-order trace check."""
import contextlib
import io
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


if __name__ == "__main__":
    unittest.main()
