// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "Interface/CInterface.h"
#include "SoundBridge.hpp"
#include <vector>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <float.h>

namespace {
using namespace tests::battle;

void clean_globals() {
    MG_CHECK(g_MatrixMap == nullptr && g_MatrixHeap == nullptr && g_MatrixData == nullptr);
    MG_CHECK(g_IFaceList == nullptr && g_D3DD == nullptr && g_Cache == nullptr);
    MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
    MG_CHECK(!SoundBridge::available());
}

enum class Scenario { Move, Replace, Remove };

struct Trace {
    std::filesystem::path path;
    std::ofstream file;
    std::vector<std::string> snapshots;
    explicit Trace(const Session &session, Profile profile, const char *name, int run) {
        const auto directory = std::filesystem::current_path() / "scenario-traces";
        std::filesystem::create_directories(directory);
        path = directory / (std::string(name) + "-" + (profile.mode == random::Mode::ParkMiller ? "pm" : "crt") +
                  "-" + std::to_string(profile.step_ms) + "-" + std::to_string(run) + ".jsonl");
        file.open(path, std::ios::trunc);
        MG_CHECK(file.good());
        file << session.metadata() << '\n';
        file.flush();
        std::cerr << "Battle profile: " << session.metadata() << "\nTrace: " << path.string() << std::endl;
    }
    void record(const Session &session) {
        snapshots.push_back(session.snapshot());
        file << "{\"type\":\"snapshot\",\"state\":" << snapshots.back() << "}\n";
        file.flush(); // Preserve the last completed boundary if a production tick crashes.
        MG_CHECK(file.good());
    }
    template<class Action>
    void command(const Session &session, const char *name, Action action) {
        const auto draws_before = random::draw_count();
        std::cerr << "Command at frame " << session.frame() << ": " << name << std::endl;
        const bool accepted = action();
        const auto draws_after = random::draw_count();
        const auto robot = session.robot(1);
        file << "{\"type\":\"command\",\"frame\":" << session.frame() << ",\"name\":\"" << name
             << "\",\"rng_draws_before\":" << draws_before << ",\"rng_draws_after\":" << draws_after
             << ",\"accepted\":" << (accepted ? "true" : "false") << ",\"resulting_order\":" << robot.order
             << ",\"resulting_target\":" << robot.target << ",\"resulting_destination\":["
             << robot.destination_x << ',' << robot.destination_y << "]}\n";
        file.flush();
        MG_CHECK(file.good());
    }
    void complete(const Session &session) {
        file << "{\"type\":\"complete\",\"frame\":" << session.frame()
             << ",\"time_ms\":" << session.time() << "}\n";
        file.flush();
        MG_CHECK(file.good());
    }
};

std::vector<std::string> scenario(Profile profile, Scenario kind, const char *name, int run, bool observe = true) {
    Session session(profile);
    Trace trace(session, profile, name, run);
    try {
        trace.command(session, "context-ground(500,300)", [&] {
            session.ground_command(500, 300);
            const auto r = session.robot(1);
            return r.order == mpo_MoveTo && r.destination_x == 48 && r.destination_y == 28;
        });
        MG_CHECK(session.robot(1).order == mpo_MoveTo);
        MG_CHECK(session.robot(1).destination_x == 48 && session.robot(1).destination_y == 28);
        bool detour = false;
        int removed_destination_x = -1, removed_destination_y = -1;
        const int limit = 15000 / profile.step_ms;
        for (int i = 0; i < limit; ++i) {
            if (i == 150 && kind == Scenario::Replace) {
                trace.command(session, "context-ground(120,500)", [&] {
                    session.ground_command(120, 500);
                    const auto r = session.robot(1);
                    return r.order == mpo_MoveTo && r.destination_x == 10 && r.destination_y == 48;
                });
                MG_CHECK(session.robot(1).order == mpo_MoveTo);
                MG_CHECK(session.robot(1).destination_x == 10 && session.robot(1).destination_y == 48);
            }
            if (i == 40 && kind == Scenario::Remove) {
                trace.command(session, "context-target(3)", [&] {
                    MG_CHECK(session.target_command(3));
                    return session.robot(1).order == mpo_Attack && session.robot(1).target == 3;
                });
                MG_CHECK(session.robot(1).order == mpo_Attack);
                MG_CHECK(session.robot(1).target == 3);
            }
            if (i == 50 && kind == Scenario::Remove) {
                removed_destination_x = session.robot(1).destination_x;
                removed_destination_y = session.robot(1).destination_y;
                trace.command(session, "delete-target(3)", [&] { return session.remove(3); });
                // Negative control: deletion leaves a non-null group reference until
                // the next scheduled player-AI update. It must not normalize to null.
                MG_CHECK(session.robot(1).target == -1);
                MG_CHECK(!session.robot(1).target_is_null);
                MG_CHECK(!session.robot(3).alive);
                const auto before = session.snapshot();
                MG_CHECK(!session.target_command(3));
                MG_CHECK(!session.remove(3));
                MG_CHECK(session.snapshot() == before);
            }
            if (observe) trace.record(session);
            session.advance();
            if (i >= 50 && kind == Scenario::Remove) {
                // TaktPL is throttled to 100 ms. The command at 400 ms and
                // update at 500 ms put the next cleanup boundary at 600 ms.
                const bool cleaned = session.time() >= 600;
                MG_CHECK(session.robot(1).target == (cleaned ? 0 : -1));
                MG_CHECK(session.robot(1).target_is_null == cleaned);
                MG_CHECK(session.robot(1).order == mpo_Attack);
                MG_CHECK(session.robot(1).destination_x == removed_destination_x);
                MG_CHECK(session.robot(1).destination_y == removed_destination_y);
            }
            for (EntityId id : {1u, 2u}) {
                const auto r = session.robot(id);
                MG_CHECK(std::isfinite(r.x) && std::isfinite(r.y));
                MG_CHECK(r.x >= 0 && r.x <= 640 && r.y >= 0 && r.y <= 640);
                MG_CHECK(!(r.x > 280 && r.x < 360 && r.y > 200 && r.y < 440));
                if (r.x > 280 && r.x < 360 && (r.y <= 200 || r.y >= 440)) detour = true;
            }
        }
        trace.record(session);
        session.check_running();
        if (kind == Scenario::Move) {
            MG_CHECK(detour);
            MG_CHECK(session.robot(1).x > 400 && session.robot(2).x > 400);
            MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        } else if (kind == Scenario::Replace) {
            MG_CHECK(session.robot(1).x < 200 && session.robot(1).y > 450);
            MG_CHECK(session.robot(2).x < 200 && session.robot(2).y > 400);
            MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        } else {
            MG_CHECK(session.robot(1).x > 450 && session.robot(1).y > 450);
        }
        trace.complete(session);
        return trace.snapshots;
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string(error.what()) + "\nBattle profile: " + session.metadata() +
                                 "\nFrame: " + std::to_string(session.frame()) + "\nTrace: " + trace.path.string());
    }
}

void movement() {
    int run = 0;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) scenario({mode, 79, step}, Scenario::Move, "movement", run++);
    clean_globals();
}
void replacement() {
    int run = 0;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        scenario({mode}, Scenario::Replace, "replacement", run++);
    clean_globals();
}
void target_removal() {
    int run = 0;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        scenario({mode}, Scenario::Remove, "target-removal", run++);
    clean_globals();
}
void compare(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    MG_CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i])
        throw std::runtime_error("First scenario divergence at boundary " + std::to_string(i) +
                                 "\nExpected: " + a[i] + "\nActual: " + b[i]);
}
void repeatability() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto a = scenario({mode}, Scenario::Move, "repeatability", 0);
        clean_globals();
        compare(a, scenario({mode}, Scenario::Move, "repeatability", 1));
        clean_globals();
    }
}
void observation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto a = scenario({mode}, Scenario::Move, "observation", 0);
        const auto b = scenario({mode}, Scenario::Move, "observation", 1, false);
        MG_CHECK(a.back() == b.back());
        clean_globals();
    }
}

void rejected_command() {
    Profile profile;
    profile.initially_selected = false;
    Session session(profile);
    const auto before = session.snapshot();
    session.ground_command(500, 300);
    MG_CHECK(session.snapshot() == before);
    MG_CHECK(!session.target_command(999));
    MG_CHECK(session.snapshot() == before);
}
void audio_unavailable() {
    Profile profile;
    profile.audio = false;
    const auto a = scenario(profile, Scenario::Move, "unavailable", 0);
    compare(a, scenario(profile, Scenario::Move, "unavailable", 1));
    clean_globals();
}
void bounds() {
    Profile profile;
    profile.max_frames = 1;
    Session session(profile);
#if defined(__MINGW32__)
    const auto initial = session.snapshot();
    _controlfp(_PC_53, _MCW_PC);
    bool precision_rejected = false;
    try { session.advance(); } catch (const std::runtime_error &) { precision_rejected = true; }
    _controlfp(_PC_24, _MCW_PC);
    MG_CHECK(precision_rejected && session.snapshot() == initial);
#endif
    session.advance();
    const auto before = session.snapshot();
    bool rejected = false;
    try { session.advance(); } catch (const std::runtime_error &) { rejected = true; }
    MG_CHECK(rejected && session.snapshot() == before);
}

void partial_setup() {
    for (auto failure : {SetupFailure::Topology, SetupFailure::Entities, SetupFailure::Selection}) {
        bool caught = false;
        try { Session session({}, failure); }
        catch (const std::runtime_error &) { caught = true; }
        MG_CHECK(caught);
        clean_globals();
        { Session session; session.advance(); }
        clean_globals();
    }
}

constexpr tests::Case cases[] = {
    {"game.scenario.movement", movement},
    {"game.scenario.partial_setup", partial_setup},
    {"game.scenario.replacement", replacement},
    {"game.scenario.target_removal", target_removal},
    {"game.scenario.repeatability", repeatability},
    {"game.scenario.observation", observation},
    {"game.scenario.rejected_command", rejected_command},
    {"game.scenario.audio_unavailable", audio_unavailable},
    {"game.scenario.bounds", bounds},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Battle scenario leaked tracked allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
