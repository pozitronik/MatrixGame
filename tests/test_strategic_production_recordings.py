"""Finite protocol and CLI contracts; no game resources or executable are required."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("strategic_reader", Path(__file__).parents[1] /
                                            "tools/check-strategic-production-recordings.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
REVISION = "a" * 40


def seal(records):
    digest = 14695981039346656037
    snapshots = [record for record in records if record["type"] == "snapshot"]
    for record in snapshots:
        payload = {key: value for key, value in record.items() if key != "type"}
        for byte in (json.dumps(payload, separators=(",", ":")) + "\n").encode():
            digest = ((digest ^ byte) * 1099511628211) & 0xffffffffffffffff
    records[-1].update(observations=len(snapshots), observation_digest_low=digest & 0xffffffff,
                       observation_digest_high=digest >> 32)
    return records


def history(ending="deploy", rng="ParkMiller", step=10):
    header = dict(type="header", schema=1, fixture="synthetic-strategic-purchase-v1", revision=REVISION, dirty=0,
                  render="storage-device-unavailable-icons-no-draw-cursor-surrogate", viewport=[1280, 720],
                  audio="unavailable", rng=rng, seed=83, step_ms=step, max_frames=2000, precision_control=131072,
                  initially_selected=False, build_exe=True, cheats=False, delayed_damage="Corrected",
                  group_counts="Corrected", place_containment="Corrected", zero_x_trace="Corrected", assert_off=False,
                  configuration="Debug", compiler="MinGW", compiler_version="13.2.0",
                  production_observation="strategic-production-v1", ending=ending,
                  navigation_layout="central-obstacle-v1", trace=False)
    result = [header]
    paid = 1000 if step == 10 else 1040
    times = dict(paid=paid, spawned=1100 if step == 10 else 1152, opened=2340 if step == 10 else 2400,
                 detached=4590 if step == 10 else 4656, closed=5840 if step == 10 else 5904)
    draws = 100
    for kind, phase, frame in CHECK.schedule(ending, step):
        time = frame * step
        if kind == "command":
            after = draws + (phase == "choose")
            result.append(dict(type=kind, name=phase, frame=frame, time_ms=time,
                               rng_draws_before=draws, rng_draws_after=after))
            draws = after
            continue
        purchased = ending != "wait-base-removed" and (time > paid or time == paid and phase != "tick")
        paid_sample = purchased and (phase != "strategy" or time > paid)
        deleted = ending == "wait-base-removed" and phase not in ("initial", "choice") or (
            ending == "paid-base-removed" and (time > paid or phase == "base-deleted"))
        initialized = ending == "deploy" and time >= times["spawned"]
        detached = ending == "deploy" and time >= times["detached"]
        queued = purchased and not deleted and not initialized
        if initialized:
            draws = 104
        actors = [dict(id=index, alive=True, x_bits=0, y_bits=0, side=1 if index < 3 else 2, state=2,
                       health_bits=1120403456, in_place=False, group=-1, order=-1, target=0,
                       target_is_null=True, destination=[-1, -1], queue=[]) for index in range(1, 5)]
        payouts = 0 if ending == "wait-base-removed" else min(frame * 10 // (paid // step), 10)
        if ending == "wait-base-removed":
            balance = 90
        elif ending == "paid-base-removed" and deleted:
            balance = 120
        elif time <= paid:
            balance = 90 + payouts * 3 - (120 if purchased else 0)
        else:
            balance = ((frame - paid // step) * (48 if step == 10 else 46)
                       // (times["closed"] // step - paid // step)) * 3
        base = dict(id=5, present=not deleted)
        if not deleted:
            state = 0 if not initialized or time >= times["closed"] else (
                1 if time < times["opened"] else 2 if not detached else 3)
            base.update(in_logic=True, side=1, state=state, floor_bits=1065353216 if state == 2 else 0,
                        spawning=initialized and not detached, queue_count=int(queued), queue=[6] if queued else [])
        robot = dict(id=6, created=purchased, present=purchased and not deleted, initialized=initialized,
                     in_queue=queued, in_logic=initialized)
        if initialized:
            robot.update(alive=True, state=2 if detached else 1 if time >= times["opened"] else 0,
                         side=1, x_bits=0, y_bits=0, base=0 if detached else 5, manual_disabled=not detached,
                         queue=[[1, 0, 0, 0, 0xffffffff, -1, 0]])
        progress = dict(payouts=payouts)
        for key, when in times.items():
            progress[key] = when if (key == "paid" and paid_sample or
                                    ending == "deploy" and key != "paid" and time >= when) else -1
        record = dict(type="snapshot", state=dict(frame=frame, time_ms=time, rng_draws=draws, robots=actors,
                                                  player_groups=[]), phase=phase, resources=[balance] * 4,
                      strategy=dict(waiting=-1 if phase == "initial" or purchased else 0,
                                    last_built=0 if purchased else -1, build_count=int(initialized)),
                      base=base, robot=robot, progress=progress)
        if paid_sample:
            record["assembly"] = dict(price=[120] * 4, weapons=[[2, 2, 10], [3, 2, 10]])
        if ending == "wait-base-removed" and deleted:
            record["capacity_sample"] = 0
        if detached:
            record["detach_distance_squared_bits"] = 1167663104  # float32 4900, the existing boundary.
        result.append(record)
    last = result[-1]["state"]
    result.append(dict(type="complete", **{key: last[key] for key in ("frame", "time_ms", "rng_draws")}))
    return seal(result)


class StrategicRecordingContracts(unittest.TestCase):
    def check(self, records, **extra):
        header = records[0]
        parsed = CHECK.trace.read_records(io.StringIO("".join(json.dumps(r) + "\n" for r in records)))
        CHECK.validate(parsed, ending=header["ending"], rng=header["rng"], step=header["step_ms"], **extra)

    def corrupt(self, mutation, ending="deploy"):
        records = history(ending)
        mutation(records)
        seal(records)
        with self.assertRaises(CHECK.trace.TraceError):
            self.check(records)

    def test_all_twelve_complete_profiles(self):
        for _, profile in CHECK.inventory():
            with self.subTest(profile=profile):
                self.check(history(**profile), revision=REVISION)

    def test_opaque_and_retired_storage(self):
        self.corrupt(lambda r: r[1]["robot"].update(state=2))
        self.corrupt(lambda r: next(x for x in r if x.get("phase") == "paid")["robot"].update(queue=[]))
        self.corrupt(lambda r: next(x for x in r if x.get("phase") == "base-deleted")["base"].update(state=0),
                     "paid-base-removed")

    def test_physical_order_full_shape_and_domains(self):
        for column, value in ((0, 9), (1, True), (2, -1), (4, "0"), (5, 0x80000000), (6, 7)):
            self.corrupt(lambda r: r[-2]["robot"]["queue"][0].__setitem__(column, value))
        self.corrupt(lambda r: r[-2]["robot"]["queue"][0].pop())
        self.corrupt(lambda r: r[-2]["robot"].update(queue=[[0] * 7] * 6))

    def test_live_session_semantics_and_logical_nine(self):
        records = history()
        records[-2]["state"]["robots"][0]["order"] = 9
        records[-2]["state"]["player_groups"] = [dict(slot=0, stored_count=1, members=1, order=9, target=0)]
        self.check(seal(records))
        for field, value in (("alive", 1), ("state", 5), ("order", 10), ("target", 5), ("side", 99), ("side", True)):
            self.corrupt(lambda r: r[-2]["state"]["robots"][0].__setitem__(field, value))
        self.corrupt(lambda r: r[-2]["state"]["robots"][0].update(queue=[[0, 0, True, 0, 0]]))

    def test_input_and_observation_order(self):
        self.corrupt(lambda r: r[2].update(name="build"))
        self.corrupt(lambda r: r[3].update(phase="initial"))
        self.corrupt(lambda r: r[2].update(time_ms=10))
        self.corrupt(lambda r: r[2].update(rng_draws_after=999))
        self.corrupt(lambda r: r[3]["state"].update(frame=True))

    def test_purchase_refund_and_assembly(self):
        self.corrupt(lambda r: next(x for x in r if x.get("phase") == "paid")["assembly"]["price"].__setitem__(0, 119))
        self.corrupt(lambda r: next(x for x in r if x.get("phase") == "paid")["assembly"].pop("weapons"))
        self.corrupt(lambda r: r[-2].update(resources=[119] * 4), "paid-base-removed")
        self.corrupt(lambda r: r[-2]["strategy"].update(waiting=-1), "wait-base-removed")
        self.corrupt(lambda r: r[-2].pop("capacity_sample"), "wait-base-removed")

    def test_lifetime_and_animation_milestones(self):
        self.corrupt(lambda r: r[-2]["robot"].update(manual_disabled=True))
        self.corrupt(lambda r: r[-2]["robot"].update(base=5))
        self.corrupt(lambda r: r[-2]["robot"].update(side=99))
        self.corrupt(lambda r: r[-2]["base"].update(side=True))
        self.corrupt(lambda r: r[-2]["progress"].update(closed=5830))
        self.corrupt(lambda r: r[-2].update(detach_distance_squared_bits=0))
        self.corrupt(lambda r: r[-2]["base"].update(queue_count=1))

    def test_checked_tail_and_digest(self):
        records = history()
        records[-1]["observation_digest_low"] ^= 1
        with self.assertRaisesRegex(CHECK.trace.TraceError, "digest"):
            self.check(records)
        for mutation in (lambda r: r.pop(), lambda r: r.append(dict(type="command", name="late", frame=584))):
            records = history()
            mutation(records)
            with self.assertRaises(CHECK.trace.TraceError):
                self.check(records)

    def test_compiler_version_types(self):
        for compiler, values in (("MSVC", (True, "1944", 0, -1, 0x80000000)),
                                 ("MinGW", (True, 1320, ""))):
            for value in values:
                self.corrupt(lambda r: r[0].update(compiler=compiler, compiler_version=value))
        records = history()
        records[0].update(compiler="MSVC", compiler_version=1944)
        self.check(records)

    def test_original_profile_and_logical_group_capacity(self):
        records = history()
        for field in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
            records[0][field] = "Original"
        records[-2]["state"]["player_groups"] = [dict(slot=60, stored_count=1, members=1, order=9, target=0)]
        self.check(seal(records))
        self.corrupt(lambda r: r[0].update(delayed_damage="Legacy"))
        self.corrupt(lambda r: r[-2]["state"].update(player_groups=[
            dict(slot=61, stored_count=1, members=1, order=9, target=0)]))

    def test_unpinned_source_identity_is_still_mandatory(self):
        for fields in (dict(dirty=True), dict(dirty="0"), dict(dirty=2), dict(revision=""),
                       dict(revision=123), dict(revision="a" * 12)):
            self.corrupt(lambda r: r[0].update(fields))
        self.corrupt(lambda r: r[0].pop("revision"))
        self.corrupt(lambda r: r[0].pop("dirty"))
        records = history()
        records[0]["dirty"] = 1
        self.check(records)

    def test_strict_source_identity(self):
        for fields in (dict(dirty=1), dict(dirty=False), dict(revision="a" * 12), dict(revision="b" * 40)):
            records = history()
            records[0].update(fields)
            with self.assertRaises(CHECK.trace.TraceError):
                self.check(records, revision=REVISION)

    def test_directory_cli_and_independent_historical_reference(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "candidate"
            reference = Path(temporary) / "reference"
            directory.mkdir()
            reference.mkdir()
            for name, profile in CHECK.inventory():
                records = history(**profile)
                (directory / name).write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")
                records[0]["revision"] = "b" * 40
                (reference / name).write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")
            def run(*args):
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    return CHECK.main([str(directory), "--revision", REVISION, *args])
            self.assertEqual(run(), 0)
            self.assertEqual(run("--reference", str(reference), "--reference-revision", "b" * 40), 0)
            self.assertEqual(run("--reference", str(reference)), 0)
            self.assertEqual(run("--reference-revision", "b" * 40), 1)
            self.assertEqual(run("--reference", str(reference), "--reference-revision", REVISION), 1)
            name = next(CHECK.inventory())[0]
            path = reference / name
            records = CHECK.read(path)
            for fields in (dict(dirty=1), dict(revision="c" * 40), dict(revision="b" * 12)):
                changed = copy.deepcopy(records)
                changed[0].update(fields)
                path.write_text("".join(json.dumps(r) + "\n" for r in changed), encoding="utf-8")
                self.assertEqual(run("--reference", str(reference)), 1)
            # Equal corrupted payloads must fail semantic validation before reference equality can help them.
            profile = next(CHECK.inventory())[1]
            for folder, revision in ((directory, REVISION), (reference, "b" * 40)):
                changed = history(**profile)
                changed[0]["revision"] = revision
                changed[-2]["state"]["robots"][0]["side"] = 99
                seal(changed)
                (folder / name).write_text("".join(json.dumps(r) + "\n" for r in changed), encoding="utf-8")
            self.assertEqual(run("--reference", str(reference), "--reference-revision", "b" * 40), 1)
            (directory / name).unlink()
            self.assertEqual(run(), 1)


if __name__ == "__main__":
    unittest.main()
