// MatrixGame - licensed under GPLv2 or any later version.
#include "ai_war_detour_recording.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "Logic/MatrixEnvironment.h"
#include <bit>
#include <sstream>
#include <vector>

namespace tests::war_recording {
namespace {
struct Completion {
    std::filesystem::path path;
    std::string tail;
};
std::vector<Completion> pending;
const char *change_name(Change change) {
    switch (change) {
    case Change::None: return "control";
    case Change::Retirement: return "retirement";
    case Change::Move: return "replacement";
    }
    throw std::logic_error("Unknown War detour change");
}
const char *role_name(Role role) {
    switch (role) {
    case Role::First: return "first";
    case Role::Replay: return "replay";
    case Role::Intact: return "intact";
    }
    throw std::logic_error("Unknown War detour role");
}
std::filesystem::path path(Identity identity) {
    return std::filesystem::current_path() / "scenario-traces" /
        (std::string("war-detour-") + change_name(identity.caller) + '-' +
         (identity.mode == random::Mode::ParkMiller ? "pm-" : "crt-") + std::to_string(identity.step) + '-' +
         role_name(identity.role) + ".jsonl");
}
CMatrixRobotAI *registered(const battle::Session &session, unsigned id) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(id, object->AsRobot())) return object->AsRobot();
    return nullptr;
}
void orders(std::ostream &out, const battle::Session &session, CMatrixRobotAI &source) {
    out << '[';
    for (int i = 0; i < source.GetOrdersInPool(); ++i) {
        if (i) out << ',';
        const auto &order = *source.GetOrder(i);
        out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param1)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param2)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4
            << ',' << object_id(session, order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
    }
    out << ']';
}
std::string observe(const battle::Session &session, const SObjectCore &core, int pulses, float health,
                    CPoint firing_place, int firing_target) {
    auto *observer = registered(session, 1);
    MG_CHECK(observer && observer->IsLiveRobot());
    const auto draws = random::draw_count();
    CPoint move(-1, -1);
    const bool moving = observer->GetMoveToCoords(move);
    const auto &env = *observer->GetEnv();
    auto *target = core.m_Object;
    const bool live = target && target->IsRobot() && target->AsRobot()->IsLiveRobot();
    std::ostringstream out;
    out << std::boolalpha << "{\"moving\":" << moving << ",\"move\":[" << move.x << ',' << move.y
        << "],\"fire_order\":" << observer->FindOrderLikeThat(ROT_FIRE)
        << ",\"environment_target\":" << object_id(session, env.m_Target)
        << ",\"attack_target\":" << object_id(session, env.m_TargetAttack)
        << ",\"previous_firing_target\":" << firing_target
        << ",\"pulses\":" << pulses << ",\"last_health_bits\":" << std::bit_cast<std::uint32_t>(health)
        << ",\"firing_place\":[" << firing_place.x << ',' << firing_place.y
        << "],\"target_core\":{\"present\":" << (target != nullptr) << ",\"id\":" << object_id(session, target)
        << ",\"live\":" << live;
    if (target) {
        MG_CHECK(target->IsRobot());
        const auto &robot = *target->AsRobot();
        out << ",\"state\":" << int(robot.m_CurrState) << ",\"must_die\":" << robot.IsMustDie();
        // DIP reuses unit storage. Its core, state and flag remain valid; live fields do not.
        if (live) out << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(robot.GetHitPoint());
    }
    out << "},\"full_orders\":[";
    for (unsigned id = 1; id <= 4; ++id) {
        if (id != 1) out << ',';
        auto *robot = registered(session, id);
        const bool alive = robot && robot->IsLiveRobot();
        out << "{\"id\":" << id << ",\"live\":" << alive;
        if (alive) {
            out << ",\"orders\":";
            orders(out, session, *robot);
        }
        out << '}';
    }
    out << "]}";
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}
} // namespace

int object_id(const battle::Session &session, const CMatrixMapStatic *object) {
    if (!object) return 0;
    // Compare against registered objects before any dereference of an obsolete reference.
    for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic()) {
        if (live != object || !live->IsRobot()) continue;
        for (unsigned id = 1; id <= 4; ++id)
            if (session.matches(id, live->AsRobot())) return static_cast<int>(id);
    }
    return -1;
}
void invalidate(random::Mode mode, int step, Change caller) {
    std::filesystem::create_directories(path({mode, step, caller, Role::First}).parent_path());
    for (Role role : {Role::First, Role::Replay, Role::Intact}) {
        if (caller == Change::None && role == Role::Intact) continue;
        std::ofstream empty(path({mode, step, caller, role}), std::ios::trunc);
        MG_CHECK(empty.good());
    }
}
Recording::Recording(Identity identity) : identity_(identity), path_(path(identity)), file_(path_, std::ios::trunc) {
    MG_CHECK(file_.good());
}
void Recording::begin(const battle::Session &session, const SObjectCore &core) {
    const auto draws = random::draw_count();
    auto header = session.metadata("reachable-war-detours-v1");
    header.pop_back();
    file_ << header << ",\"recording\":\"successful-war-detour-v1\",\"caller\":\""
          << change_name(identity_.caller) << "\",\"role\":\"" << role_name(identity_.role)
          << "\",\"change\":\"" << change_name(identity_.role == Role::Intact ? Change::None : identity_.caller)
          << "\",\"change_time_ms\":800,\"initial_leg\":[48,8],\"patrol_to\":[48,38],\"trace\":"
#ifdef _TRACE
          << "true"
#else
          << "false"
#endif
          << "}\n";
    snapshot(session, core, "initial", 0, 20, CPoint(-1, -1), 0);
    MG_CHECK(random::draw_count() == draws);
}
void Recording::command(const battle::Session &session, const SObjectCore &core, std::string_view name,
                        std::uint64_t before, int pulses, float health, CPoint firing_place) {
    const auto after = random::draw_count();
    const bool patrol = name == "setup-patrol", retirement = name == "target-retirement";
    file_ << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session.frame()
          << ",\"time_ms\":" << session.time() << ",\"route\":\""
          << (patrol ? "player-group" : retirement ? "delete" : "context-ground")
          << "\",\"target\":" << (retirement ? 3 : 0) << ",\"point\":["
          << (patrol ? "48,38" : retirement ? "-1,-1" : name == "setup-move" ? "500,100" : "500,400")
          << "],\"rng_draws_before\":" << before << ",\"rng_draws_after\":" << after << "}\n";
    snapshot(session, core, "command", pulses, health, firing_place, 0);
    MG_CHECK(random::draw_count() == after);
}
void Recording::snapshot(const battle::Session &session, const SObjectCore &core, std::string_view phase,
                         int pulses, float health, CPoint firing_place, int firing_target,
                         std::string_view original) {
    file_ << "{\"type\":\"snapshot\",\"phase\":\"" << phase << "\",\"state\":";
    if (original.empty()) file_ << session.snapshot();
    else {
        const auto split = original.find('\n');
        MG_CHECK(split != std::string_view::npos);
        file_ << original.substr(0, split) << ",\"detail\":" << original.substr(split + 1);
    }
    file_ << ",\"war_detour\":" << observe(session, core, pulses, health, firing_place, firing_target) << "}\n";
    MG_CHECK(file_.good());
}
void Recording::tick(const battle::Session &session, const SObjectCore &core, std::string_view original,
                     int pulses, float health, CPoint firing_place, int firing_target) {
    snapshot(session, core, "tick", pulses, health, firing_place, firing_target, original);
}
void Recording::finish(const battle::Session &session, std::string_view milestones) {
    std::ostringstream tail;
    tail << "{\"type\":\"complete\",\"frame\":" << session.frame() << ",\"time_ms\":" << session.time()
         << ",\"rng_draws\":" << random::draw_count() << ",\"milestones\":" << milestones << "}\n";
    file_.flush();
    MG_CHECK(file_.good());
    file_.close();
    MG_CHECK(!file_.fail());
    pending.push_back({path_, tail.str()});
}
void complete() {
    for (const auto &result : pending) {
        std::ofstream file(result.path, std::ios::app);
        file << result.tail;
        file.flush();
        MG_CHECK(file.good());
        file.close();
        MG_CHECK(!file.fail());
    }
    pending.clear();
}
} // namespace tests::war_recording
