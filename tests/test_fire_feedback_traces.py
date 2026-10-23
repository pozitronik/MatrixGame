"""Independent semantic guards for equally incomplete fire-feedback recordings."""
import copy
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("fire_feedback_traces", Path(__file__).with_name("check_fire_feedback_traces.py"))
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def observation():
    robots = [{"id": i, "alive": True, "x_bits": 1120403456, "y_bits": 1128792064, "side": 1 if i < 3 else 2,
               "state": 2, "health_bits": 1120403456, "in_place": False, "group": 0 if i < 3 else -1,
               "order": 1 if i < 3 else -1, "target": 0, "target_is_null": True, "destination": [8, 8],
               "queue": [[1, 0, 1090519040, 1090519040, 0]]} for i in (1, 2, 3)] + [{"id": 4, "alive": False}]
    feedback = [{"id": i, "place": -1, "place_not_found": -1000000, "bad_places": [], "enemy_count": 0,
                 "target": -1, "attack_target": 0, "target_change": 0, "last_fire": 0, "last_hit": 0,
                 "health_bits": 1120403456, "moving": False, "move": [-1, -1]} for i in (1, 2, 3)]
    return {"type": "snapshot", "state": {"frame": 2, "time_ms": 20, "rng_draws": 300, "robots": robots,
            "player_groups": [{"slot": 0, "stored_count": 2, "members": 2, "order": 1, "target": 0}]}, "feedback": feedback, "war": False, "ally_on_ray": False}


def terminal():
    return {"type": "complete", "frame": 2, "time_ms": 20, "feedback_result": {"first_bad": 0, "first_relocation": 0, "first_recovery_hit": 0, "final_health_bits": 1120403456}}


class FeedbackSemantics(unittest.TestCase):
    def test_signed_unassigned_time_and_nonnull_unknown_target_are_preserved(self):
        CHECK.observations(observation())

    def test_both_equal_files_missing_live_actor_fail_independently(self):
        left, right = observation(), observation()
        left["feedback"].pop()
        right["feedback"].pop()
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "feedback actor inventory"):
                CHECK.observations(value)

    def test_both_equal_files_missing_custom_field_fail_independently(self):
        left, right = observation(), observation()
        del left["feedback"][0]["last_fire"]
        del right["feedback"][0]["last_fire"]
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "missing custom feedback"):
                CHECK.observations(value)

    def test_retired_session_id_cannot_disappear_or_be_reused(self):
        value = observation()
        value["state"]["robots"].pop()
        with self.assertRaisesRegex(ValueError, "Session actor inventory"):
            CHECK.observations(value)
        value = observation()
        value["state"]["robots"][-1]["alive"] = True
        with self.assertRaisesRegex(ValueError, "live/retired"):
            CHECK.observations(value)

    def test_boolean_id_or_timestamp_is_not_an_integer(self):
        for key in ("id", "last_fire"):
            value = observation()
            value["feedback"][0][key] = True
            with self.assertRaises(ValueError):
                CHECK.observations(value)

    def test_both_equal_files_missing_live_position_queue_or_group_fail(self):
        for field in ("x_bits", "queue", "group"):
            left, right = observation(), observation()
            del left["state"]["robots"][0][field]
            del right["state"]["robots"][0][field]
            self.assertEqual(left, right)
            for value in (left, right):
                with self.assertRaisesRegex(ValueError, "missing live Session actor field"):
                    CHECK.observations(value)

    def test_both_equal_files_missing_player_groups_or_group_fields_fail(self):
        for field in ("player_groups", "members", "order"):
            left, right = observation(), observation()
            for value in (left, right):
                if field == "player_groups":
                    del value["state"][field]
                else:
                    del value["state"]["player_groups"][0][field]
            self.assertEqual(left, right)
            for value in (left, right):
                with self.assertRaisesRegex(ValueError, "missing Session"):
                    CHECK.observations(value)

    def test_live_session_types_full_queues_and_group_inventory_are_required(self):
        edits = (lambda r: r["state"]["robots"][0].__setitem__("x_bits", True),
                 lambda r: r["state"]["robots"][0].__setitem__("side", "1"),
                 lambda r: r["state"]["robots"][0].__setitem__("destination", [8]),
                 lambda r: r["state"]["robots"][0].__setitem__("queue", [[1, 0, 0, 0]]),
                 lambda r: r["state"]["robots"][0].__setitem__("queue", [[1, 0, True, 0, 0]]),
                 lambda r: r["state"].__setitem__("player_groups", []),
                 lambda r: r["state"]["player_groups"][0].__setitem__("members", True))
        for edit in edits:
            value = observation()
            edit(value)
            with self.assertRaises(ValueError):
                CHECK.observations(value)

    def test_custom_health_is_bound_to_its_session_actor(self):
        value = observation()
        value["feedback"][2]["health_bits"] ^= 1
        with self.assertRaisesRegex(ValueError, "health differs"):
            CHECK.observations(value)

    def test_movement_war_and_ray_are_required_typed_observations(self):
        for key in ("moving", "move", "war", "ally_on_ray"):
            value = observation()
            if key in ("moving", "move"):
                del value["feedback"][0][key]
            else:
                del value[key]
            with self.assertRaises((ValueError, KeyError)):
                CHECK.observations(value)

    def test_both_equal_footers_missing_milestone_fail_independently(self):
        left, right = terminal(), terminal()
        del left["feedback_result"]["first_recovery_hit"]
        del right["feedback_result"]["first_recovery_hit"]
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "terminal feedback"):
                CHECK.completion(value, observation())

    def test_footer_boundary_health_and_milestone_types_are_checked(self):
        CHECK.completion(terminal(), observation())
        for edit in (lambda r: r.__setitem__("frame", 3),
                     lambda r: r["feedback_result"].__setitem__("final_health_bits", 0),
                     lambda r: r["feedback_result"].__setitem__("first_bad", True)):
            value = copy.deepcopy(terminal())
            edit(value)
            with self.assertRaises(ValueError):
                CHECK.completion(value, observation())


if __name__ == "__main__":
    unittest.main()