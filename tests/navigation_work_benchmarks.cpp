// MatrixGame - licensed under GPLv2 or any later version.
#include "benchmark_support.hpp"
#include "navigation_work_scene.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <algorithm>
#include <cstdlib>
#include <float.h>
#include <iostream>
#include <limits>

namespace {
using namespace tests::navigation_work;

void count(bench::Counters &out, std::string_view name, std::uint64_t value) {
    MG_CHECK(value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    out.add(name, static_cast<std::int64_t>(value));
}

class NavigationSession final : public bench::Session {
public:
    explicit NavigationSession(Options options) : options_(options) {}
    void prepare() override {
        scene_.reset();
        scene_ = std::make_unique<Scene>(options_);
    }
    void run(bench::Counters &work) override {
        // Timed: production ticks plus the scene's contract observations, without collection.
        const auto result = scene_->run(false, false);
        count(work, "completed_map_ticks", result.events.frames);
        count(work, "elapsed_simulation_ms", result.events.time_ms);
        count(work, "arrived_robots", std::count_if(result.events.first_arrival.begin(),
              result.events.first_arrival.end(), [](int time) { return time > 0; }));
        count(work, "rng_draws", result.events.final_rng_draws - result.events.initial_rng_draws);
    }
    void inspect(bench::Counters &details) override {
        scene_.reset();
        // Untimed: fresh matching scenes, with full observations outside each capture.
        const auto inactive = tests::navigation_work::run(options_, false, true);
        const auto collected = tests::navigation_work::run(options_, true, true);
        MG_CHECK(inactive.states == collected.states && inactive.events == collected.events);
        MG_CHECK(collected.total.local_searches > 0 && collected.total.robot_collision_candidates > 0);
        for (size_t i = 0; i < fields.size(); ++i) {
            count(details, names[i], collected.total.*fields[i]);
            for (size_t interval = 0; interval < collected.intervals.size(); ++interval)
                count(details, "interval_" + std::to_string(interval) + "." + names[i],
                      collected.intervals[interval].*fields[i]);
        }
        count(details, "captured_map_ticks", collected.events.frames);
        count(details, "captured_simulation_ms", collected.events.time_ms);
        count(details, "captured_rng_draws", collected.events.final_rng_draws - collected.events.initial_rng_draws);
        count(details, "minimum_separation_bits", collected.events.minimum_separation_bits);
        for (size_t i = 0; i < 8; ++i) {
            const auto prefix = "mover_" + std::to_string(i + 1) + ".";
            count(details, prefix + "first_movement_ms", collected.events.first_movement[i]);
            count(details, prefix + "first_arrival_ms", collected.events.first_arrival[i]);
            count(details, prefix + "assigned_x", collected.events.assigned_cells[2 * i]);
            count(details, prefix + "assigned_y", collected.events.assigned_cells[2 * i + 1]);
            count(details, prefix + "final_x_bits", collected.events.final_position_bits[2 * i]);
            count(details, prefix + "final_y_bits", collected.events.final_position_bits[2 * i + 1]);
        }
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto &state : collected.states) {
            for (unsigned char byte : state) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            hash *= 1099511628211ull; // A zero-byte separator between snapshots.
        }
        count(details, "trajectory_fnv64_low", static_cast<std::uint32_t>(hash));
        count(details, "trajectory_fnv64_high", hash >> 32);
    }
private:
    Options options_;
    std::unique_ptr<Scene> scene_;
};

std::vector<bench::Workload> workloads() {
    std::vector<bench::Workload> result;
    for (auto kind : {Kind::CrowdedDestination, Kind::OpposingFlow})
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int step : {10, 16}) {
        const Options options{kind, mode, step};
        const std::string family = kind == Kind::CrowdedDestination ? "crowded_destination" : "opposing_flow";
        const std::string rng = mode == random::Mode::ParkMiller ? "park_miller" : "legacy";
        result.push_back({"navigation." + family + "." + rng + std::to_string(step),
            "32000 ms of complete production map ticks plus contract observations; collector inactive in timing, "
            "scoped operation counts inspected separately outside timing",
            {{"scene", name(kind)}, {"rng", rng}, {"seed", "81"}, {"step_ms", std::to_string(step)},
             {"topology", kind == Kind::OpposingFlow ? "open-v1" : "central-obstacle-v1"},
             {"mover_fixture_ids", "1,2,5,6,7,8,9,10"},
             {"interval_0", "(0,4000] ms"}, {"interval_1", "(4000,12000] ms"},
             {"interval_2", "(12000,32000] ms"}, {"capture", "Session::advance; setup/commands/observation excluded"}},
            1, [options] { return std::make_unique<NavigationSession>(options); }});
    }
    return result;
}
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Base::CMain::BaseInit();
#if defined(__MINGW32__)
    _controlfp(_PC_24, _MCW_PC);
#endif
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
#if defined(_MSC_VER) && defined(_DEBUG)
    tests::configure_crt_reports();
#endif
    const auto all = workloads();
    const int result = bench::run(argc, argv, all);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
