"""CLI contracts for the post-engine auto-order trace check."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

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
        for record in self.records:
            if record["type"] == "snapshot":
                record["state"]["robots"][0]["queue"] = [[1, 2, 0, 0xffffffff, 0]]
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

    def test_queue_boundaries_and_dead_actor_omission_pass(self):
        for queue in ([], [[0, 0, 0, 0xffffffff, -1]], [[8, 8, 0xffffffff, 0, 4]] * 5):
            with self.subTest(queue=queue):
                records = json.loads(json.dumps(self.records))
                for record in records:
                    if record["type"] == "snapshot":
                        record["state"]["robots"][0]["queue"] = queue
                        record["state"]["robots"].append({"id": 2, "alive": False})
                self.overwrite(records, both=True)
                self.assertEqual(0, self.run_cli())

    def assert_queue_rejected(self, queue, diagnostic):
        records = json.loads(json.dumps(self.records))
        records[2]["state"]["robots"][0]["queue"] = queue
        self.overwrite(records, both=True)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, diagnostic):
            CHECK.load(self.directory, self.names[0])
        self.assertEqual(1, self.run_cli())

    def test_queue_shape_fails_even_when_both_replays_match(self):
        for queue in (None, {}, "queue", False, 1, 1.0, [[0, 0, 0, 0, 0]] * 6):
            with self.subTest(queue=queue):
                self.assert_queue_rejected(queue, r"robots\[0\]\.queue must be an array of at most 5 orders")
        for order in (None, {}, "order", False, 0, 0.0, [], [0, 0, 0, 0], [0, 0, 0, 0, 0, 0]):
            with self.subTest(order=order):
                self.assert_queue_rejected([order], r"queue\[0\] must be a five-cell order array")
        records = json.loads(json.dumps(self.records))
        del records[2]["state"]["robots"][0]["queue"]
        self.overwrite(records, both=True)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, r"queue must be an array"):
            CHECK.load(self.directory, self.names[0])
        self.assertEqual(1, self.run_cli())

    def test_queue_scalars_fail_even_when_both_replays_match(self):
        for cell, invalid in ((0, (-1, 9)), (1, (-1, 9)), (2, (-1, 0x100000000)),
                              (3, (-1, 0x100000000)), (4, (-2, 5))):
            for value in (*invalid, True, False, "0", 0.0, None, [], {}):
                with self.subTest(cell=cell, value=value):
                    order = [1, 2, 0, 0, 0]
                    order[cell] = value
                    message = ("uint32 float bit pattern" if cell in (2, 3) else
                               "integer object ID" if cell == 4 else "integer")
                    self.assert_queue_rejected([order], rf"queue\[0\]\[{cell}\] must be (?:a|an) {message}")

    def test_queue_validator_omission_accepts_correlated_invalid_pairs(self):
        for queue in ("queue", [[1, 2, True, 0, 0]], [[1, 2, 0, 0, 0, 0]], [[9, 2, 0, 0, 0]]):
            with self.subTest(queue=queue):
                records = json.loads(json.dumps(self.records))
                records[2]["state"]["robots"][0]["queue"] = queue
                self.overwrite(records, both=True)
                with mock.patch.object(CHECK, "validate_queues"):
                    self.assertEqual(0, self.run_cli())
                self.assertEqual(1, self.run_cli())

    def test_liveness_cannot_bypass_queue_checks(self):
        for value in (None, "true", "false", 0, 1, 0.0, 1.0, [], {}):
            for missing in (False, True) if value is None else (False,):
                with self.subTest(alive=value, missing=missing):
                    records = json.loads(json.dumps(self.records))
                    actor = records[2]["state"]["robots"][0]
                    actor["alive"] = value
                    actor["queue"] = "malformed"
                    if missing:
                        del actor["alive"]
                    self.overwrite(records, both=True)
                    with self.assertRaisesRegex(CHECK.TRACES.TraceError, r"robots\[0\]\.alive must be a boolean"):
                        CHECK.load(self.directory, self.names[0])
                    with mock.patch.object(CHECK, "validate_queues"):
                        self.assertEqual(0, self.run_cli())
                    self.assertEqual(1, self.run_cli())

    def test_dead_actor_stale_queue_fails_even_when_both_replays_match(self):
        for queue in ([], [[0, 0, 0, 0, 0]], "malformed", None):
            with self.subTest(queue=queue):
                records = json.loads(json.dumps(self.records))
                records[2]["state"]["robots"][0].update(alive=False, queue=queue)
                self.overwrite(records, both=True)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, r"queue must be omitted for a dead actor"):
                    CHECK.load(self.directory, self.names[0])
                with mock.patch.object(CHECK, "validate_queues"):
                    self.assertEqual(0, self.run_cli())
                self.assertEqual(1, self.run_cli())

    def test_later_live_actor_and_queue_entry_are_checked(self):
        records = json.loads(json.dumps(self.records))
        later = dict(records[-2]["state"]["robots"][0], id=2)
        later["queue"] = [[0, 0, 0, 0, 0], [1, 2, 0, "0", -1]]
        records[-2]["state"]["robots"].append(later)
        self.overwrite(records, both=True)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, r"robots\[1\]\.queue\[1\]\[3\]"):
            CHECK.load(self.directory, self.names[0])
        self.assertEqual(1, self.run_cli())

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
