// MatrixGame - licensed under GPLv2 or any later version.
#include "player_target_recording.hpp"
#include "test_support.hpp"
#include "Interface/CInterface.h"
#include "Logic/MatrixEnvironment.h"
#include <bit>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tests::target_recording {
namespace {
struct Completion {
    std::filesystem::path path;
    std::string tail;
};
std::vector<Completion> pending;
const char *target_name(int target) {
    switch (target) {
    case -1: return "patient";
    case 0: return "friendly";
    case 1: return "enemy";
    case 2: return "ground";
    default: throw std::logic_error("Unknown target lifecycle variant");
    }
}
const char *ending_name(int ending) {
    switch (ending) {
    case 0: return "complete";
    case 1: return "remove";
    case 2: return "stop";
    default: throw std::logic_error("Unknown target lifecycle ending");
    }
}
std::filesystem::path path(Identity identity) {
    return std::filesystem::current_path() / "scenario-traces" /
        (std::string("player-target-") + (identity.target < 0 ? "repair-" : "bomb-") + target_name(identity.target) +
         "-" + ending_name(identity.ending) + "-" + (identity.mode == random::Mode::ParkMiller ? "pm-" : "crt-") +
         std::to_string(identity.step) + (identity.button ? "-button.jsonl" : "-keyboard.jsonl"));
}
int object_id(const battle::Session &session, const CMatrixMapStatic *object) {
    if (!object) return 0;
    // Compare first: an obsolete environment pointer must never be dereferenced.
    for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic()) {
        if (live != object || !live->IsRobot()) continue;
        for (unsigned id = 1; id <= 4; ++id)
            if (session.matches(id, live->AsRobot())) return static_cast<int>(id);
    }
    return -1;
}
CMatrixRobotAI *registered(const battle::Session &session, unsigned id) {
    for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic())
        if (live->IsRobot() && session.matches(id, live->AsRobot())) return live->AsRobot();
    return nullptr;
}
void orders(std::ostream &out, const battle::Session &session, CMatrixRobotAI &robot) {
    out << '[';
    for (int i = 0; i < robot.GetOrdersInPool(); ++i) {
        if (i) out << ',';
        const auto &order = *robot.GetOrder(i);
        out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param1)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param2)
            << ',' << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4
            << ',' << object_id(session, order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
    }
    out << ']';
}
std::string observe(const battle::Session &session, const Observation &value) {
    const int actor_core = value.actor_core ? (!value.actor_core->m_Object ? 0 :
        value.actor_core->m_Object == value.actor ? 1 : -1) : session.matches(1, value.actor) ? 1 : 0;
    auto *actor = actor_core == 1 ? value.actor : nullptr;
    const bool actor_live = actor && actor->IsLiveRobot();
    const bool peer_present = value.peer && session.matches(value.peer_id, value.peer);
    const bool peer_live = peer_present && value.peer->IsLiveRobot();
    std::ostringstream out;
    out << std::boolalpha << "{\"preorder_flags\":" << (g_IFaceList->m_IfListFlags &
        (PREORDER_REPAIR | PREORDER_BOMB | ORDERING_MODE)) << ",\"actor_core\":" << actor_core
        << ",\"actor\":{\"present\":" << (actor != nullptr) << ",\"live\":" << actor_live;
    if (actor) out << ",\"state\":" << int(actor->m_CurrState) << ",\"must_die\":" << actor->IsMustDie();
    // DIP reuses the unit union for debris. Read live-only state only while live.
    if (actor_live) {
        out << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(actor->m_PosX)
            << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(actor->m_PosY)
            << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(actor->GetHitPoint())
            << ",\"environment_target\":" << object_id(session, actor->GetEnv()->m_Target)
            << ",\"fire_order\":" << actor->FindOrderLikeThat(ROT_FIRE);
    }
    out << "},\"peer\":{\"id\":" << value.peer_id << ",\"present\":" << peer_present
        << ",\"live\":" << peer_live;
    if (peer_present) out << ",\"state\":" << int(value.peer->m_CurrState);
    if (peer_live) {
        out << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(value.peer->GetHitPoint())
            << ",\"max_health_bits\":" << std::bit_cast<std::uint32_t>(value.peer->GetMaxHitPoint())
            << ",\"need_repair\":" << value.peer->NeedRepair();
    }
    out << "},\"selected_orders\":[";
    for (unsigned id = 1; id <= 2; ++id) {
        if (id != 1) out << ',';
        auto *robot = id == 1 ? actor : registered(session, id);
        const bool live = robot && robot->IsLiveRobot();
        out << "{\"id\":" << id << ",\"live\":" << live;
        if (live) {
            out << ",\"orders\":";
            orders(out, session, *robot);
        }
        out << '}';
    }
    out << "],\"heals\":" << value.heals << ",\"damage_events\":" << value.damage_events
        << ",\"interrupted_at\":" << value.interrupted_at << ",\"requested_at\":" << value.requested_at
        << ",\"dying_at\":" << value.dying_at << ",\"retired_at\":" << value.retired_at
        << ",\"destination\":[" << value.destination.x << ',' << value.destination.y
        << "],\"stop_destination\":[" << value.stop_destination.x << ',' << value.stop_destination.y << "]}";
    return out.str();
}
} // namespace

void prepare(int target, int ending) {
    pending.clear();
    std::filesystem::create_directories(std::filesystem::current_path() / "scenario-traces");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16})
            for (bool button : {false, true})
                for (int finish : {0, 1, 2}) {
                    if (target >= 0 && finish != ending) continue;
                    std::ofstream file(path({mode, step, button, target, finish}), std::ios::trunc);
                    MG_CHECK(file.good());
                }
}

Recording::Recording(Identity identity) : identity_(identity), path_(path(identity)), stream_(path_, std::ios::trunc) {
    MG_CHECK(stream_.good());
}
void Recording::header(const battle::Session &session) {
    auto header = session.metadata(identity_.target < 0 ? "repair-target-lifecycle-v1" : "bomb-target-lifecycle-v1");
    header.pop_back();
    stream_ << header << ",\"recording\":\"player-target-lifecycle-v1\",\"target_kind\":\""
        << target_name(identity_.target) << "\",\"ending\":\"" << ending_name(identity_.ending)
        << "\",\"route\":\"" << (identity_.button ? "button" : "keyboard")
        << "\",\"horizon_ms\":" << (identity_.target < 0 ? 2000 : 30000) << "}\n";
    stream_.flush();
    MG_CHECK(stream_.good());
}
void Recording::command(const battle::Session &session, const char *name, const char *input, int key, int target,
                        std::array<int, 2> point, std::uint64_t before, bool accepted) {
    stream_ << std::boolalpha << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session.frame()
        << ",\"time_ms\":" << session.time() << ",\"input\":\"" << input << "\",\"key\":" << key
        << ",\"target\":" << target << ",\"point\":[" << point[0] << ',' << point[1]
        << "],\"accepted\":" << accepted << ",\"rng_draws_before\":" << before
        << ",\"rng_draws_after\":" << random::draw_count() << "}\n";
    stream_.flush();
    MG_CHECK(stream_.good());
}
void Recording::snapshot(const battle::Session &session, const char *phase, const Observation &value,
                         std::string_view state) {
    const auto draws = random::draw_count();
    std::string owned;
    if (state.empty()) {
        owned = session.snapshot();
        state = owned;
    }
    stream_ << "{\"type\":\"snapshot\",\"phase\":\"" << phase << "\",\"state\":" << state
        << ",\"target_lifecycle\":" << observe(session, value) << "}\n";
    stream_.flush();
    MG_CHECK(stream_.good() && random::draw_count() == draws);
}
void Recording::finish(const battle::Session &session, const Observation &value) {
    const auto draws = random::draw_count();
    std::ostringstream tail;
    tail << "{\"type\":\"complete\",\"frame\":" << session.frame() << ",\"time_ms\":" << session.time()
        << ",\"result\":" << observe(session, value) << "}\n";
    pending.push_back({path_, tail.str()});
    MG_CHECK(random::draw_count() == draws);
}
void complete_after_cleanup() {
    for (const auto &completion : pending) {
        std::ofstream file(completion.path, std::ios::app);
        MG_CHECK(file.good());
        file << completion.tail;
        file.flush();
        MG_CHECK(file.good());
    }
    pending.clear();
}
} // namespace tests::target_recording
