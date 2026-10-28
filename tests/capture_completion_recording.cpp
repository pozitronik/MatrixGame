// MatrixGame - licensed under GPLv2 or any later version.
#include "capture_completion_recording.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Interface/CInterface.h"
#include <bit>
#include <filesystem>
#include <sstream>
#include <vector>

namespace tests::capture_completion {
namespace {
using battle::Session;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
const char *route_name(int route) {
    MG_CHECK(route >= 0 && route <= 2);
    return route == 0 ? "context" : route == 1 ? "keyboard" : "button";
}
Caller caller(bool base, int owner, int route) {
    return route == 1 ? Caller::Keyboard : route == 2 ? Caller::Button : base ? Caller::Base :
           owner == 0 ? Caller::NeutralFactory : Caller::EnemyFactory;
}
const char *caller_name(Caller value) {
    switch (value) {
    case Caller::NeutralFactory: return "game.outcome.registered_capture";
    case Caller::EnemyFactory: return "game.outcome.registered_enemy_capture";
    case Caller::Base: return "game.outcome.registered_base_capture";
    case Caller::Keyboard: return "game.player.capture_keyboard_completion";
    case Caller::Button: return "game.player.capture_button_completion";
    }
    throw std::runtime_error("Unknown capture caller");
}
std::string path(bool base, int owner, int route, random::Mode mode, int step) {
    return std::string("scenario-traces/capture-command-") + (base ? "base" : "factory") + "-" +
           (owner == 0 ? "neutral" : "enemy") + "-" + route_name(route) + "-" +
           (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" + std::to_string(step) + ".jsonl";
}
struct Completion {
    std::string path;
    int frame, time;
    std::uint64_t draws;
};
std::vector<Completion> pending;
std::array<CMatrixMapStatic *, 2> capture_queue_roles(CMatrixBuilding &building) {
    std::array<CMatrixMapStatic *, 2> result{};
    auto *item = building.m_BS.GetTopItem();
    for (unsigned index = 0; item; ++index) {
        MG_CHECK(index < result.size());
        result[index] = item;
        item = item->m_NextStackItem;
    }
    return result;
}
std::string capture_observation(Session &session, CMatrixBuilding &building,
        const std::array<CMatrixMapStatic *, 2> &roles, const char *phase, std::array<int, 3> progress) {
    const auto draws = random::draw_count();
    std::array<CMatrixRobotAI *, 4> actors{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        for (unsigned id = 1; id <= 4; ++id)
            if (session.matches(id, object->AsRobot())) actors[id - 1] = object->AsRobot();
    }
    std::array<CMatrixMapStatic *, 2> queued{};
    auto *item = building.m_BS.GetTopItem();
    unsigned queued_count = 0;
    while (item) {
        MG_CHECK(queued_count < queued.size());
        queued[queued_count++] = item;
        item = item->m_NextStackItem;
    }
    const auto identity = [&](const CMatrixMapStatic *object) {
        if (!object) return 0;
        if (object == &building) return 5;
        for (unsigned index = 0; index < actors.size(); ++index)
            if (object == actors[index]) return int(index + 1);
        for (auto *current : queued)
            if (current && object == current)
                for (unsigned index = 0; index < roles.size(); ++index)
                    if (object == roles[index]) return int(index + 6);
        return -1;
    };
    std::ostringstream out;
    out << "{\"phase\":\"" << phase << "\",\"state\":" << session.snapshot()
        << ",\"milestones\":[" << progress[0] << ',' << progress[1] << ',' << progress[2]
        << "],\"paused\":" << (g_MatrixMap->IsPaused() ? "true" : "false")
        << ",\"player_status\":" << int(side().GetStatus()) << ",\"preorder\":"
        << (g_IFaceList->m_IfListFlags & (PREORDER_CAPTURE | ORDERING_MODE))
        << ",\"building\":{\"id\":5,\"owner\":" << building.GetSide() << ",\"kind\":" << int(building.m_Kind)
        << ",\"paint\":" << building.m_TrueColor.m_ColoredCnt << ",\"color\":" << building.m_TrueColor.m_Color
        << ",\"capturer\":" << identity(building.m_Capturer) << ",\"keepalive\":" << building.m_InCaptureTime;
    if (building.IsBase())
        out << ",\"base_state\":" << int(building.m_State) << ",\"floor_bits\":"
            << std::bit_cast<uint32_t>(building.m_BaseFloor);
    out << ",\"queue_count\":" << building.m_BS.GetItemsCnt() << ",\"queue\":[";
    for (unsigned index = 0; index < queued_count; ++index) {
        if (index) out << ',';
        // Templates are opaque: only queue-owned identity/order and explicitly set side are read.
        out << "{\"id\":" << identity(queued[index]) << ",\"owner\":" << queued[index]->GetSide() << '}';
    }
    out << "]},\"balances\":[";
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        if (resource) out << ',';
        out << '[' << side().GetResourcesAmount(static_cast<ERes>(resource)) << ','
            << g_MatrixMap->GetSideById(2)->GetResourcesAmount(static_cast<ERes>(resource)) << ']';
    }
    out << "],\"logic_order\":[";
    bool emitted = false;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (emitted) out << ',';
        out << identity(object);
        emitted = true;
    }
    out << "],\"actors\":[";
    for (unsigned index = 0; index < actors.size(); ++index) {
        if (index) out << ',';
        auto *robot = actors[index];
        out << "{\"id\":" << index + 1 << ",\"present\":" << (robot ? "true" : "false");
        if (robot) {
            const bool alive = robot->IsLiveRobot();
            out << ",\"alive\":" << (alive ? "true" : "false") << ",\"state\":" << int(robot->m_CurrState);
            if (alive) {
                const int slot = robot->GetGroupLogic();
                MG_CHECK(slot >= -1 && slot < MAX_LOGIC_GROUP);
                auto *group = slot >= 0 && robot->m_Side == PLAYER_SIDE ? &side().m_PlayerGroup[slot] : nullptr;
                out << ",\"x_bits\":" << std::bit_cast<uint32_t>(robot->m_PosX)
                    << ",\"y_bits\":" << std::bit_cast<uint32_t>(robot->m_PosY)
                    << ",\"health_bits\":" << std::bit_cast<uint32_t>(robot->GetHitPoint())
                    << ",\"group\":" << slot << ",\"group_target\":" << identity(group ? group->m_Obj : nullptr)
                    << ",\"group_order\":" << (group ? int(group->Order()) : -1)
                    << ",\"destination\":[" << (group ? group->m_To.x : -1) << ',' << (group ? group->m_To.y : -1)
                    << "],\"queue\":[";
                const int count = robot->GetOrdersInPool();
                MG_CHECK(count >= 0 && count <= MAX_ORDERS);
                int capture_target = 0;
                for (int order_index = 0; order_index < count; ++order_index) {
                    if (order_index) out << ',';
                    const auto &order = *robot->GetOrder(order_index);
                    const int target = identity(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr);
                    if (order.m_OrderType == ROT_CAPTURE_FACTORY && !capture_target) capture_target = target;
                    out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                        << std::bit_cast<uint32_t>(order.m_Param1) << ',' << std::bit_cast<uint32_t>(order.m_Param2)
                        << ',' << std::bit_cast<uint32_t>(order.m_Param3) << ',' << order.m_Param4
                        << ',' << target << ']';
                }
                out << "],\"capture_target\":" << capture_target;
            }
        }
        out << '}';
    }
    out << "]}";
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}

} // namespace
void invalidate(Caller selected) {
    std::filesystem::create_directories("scenario-traces");
    for (bool base : {false, true}) for (int owner : {0, 2}) for (int route : {0, 1, 2}) {
        if (caller(base, owner, route) != selected) continue;
        for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
            std::ofstream file;
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.open(path(base, owner, route, mode, step), std::ios::trunc);
            file.close();
        }
    }
}
Recording::Recording(Session &session, CMatrixBuilding &building, int owner, int route, random::Mode mode, int step)
    : session_(session), building_(building), roles_(capture_queue_roles(building)),
      path_(path(building.IsBase(), owner, route, mode, step)) {
    stream_.rdbuf()->pubsetbuf(buffer_.data(), buffer_.size());
    stream_.exceptions(std::ios::badbit | std::ios::failbit);
    stream_.open(path_, std::ios::trunc);
    const auto fixture = building.IsBase() ? "base-capture-route-completion-v2" : "capture-route-completion-v1";
    auto header = session.metadata(fixture);
    header.pop_back();
    stream_ << header << ",\"kind\":" << int(building.m_Kind) << ",\"initial_owner\":" << owner
            << ",\"route\":\"" << route_name(route) << "\",\"caller\":\""
            << caller_name(caller(building.IsBase(), owner, route))
            << "\",\"navigation_layout\":\"central-obstacle-v1\""
#ifdef _TRACE
            << ",\"trace\":true}\n";
#else
            << ",\"trace\":false}\n";
#endif
}
Input Recording::input() const {
    MG_CHECK(g_MatrixMap->m_TraceStopObj == &building_);
    const auto &position = g_MatrixMap->m_TraceStopPos;
    return {random::draw_count(), {std::bit_cast<uint32_t>(position.x), std::bit_cast<uint32_t>(position.y),
            std::bit_cast<uint32_t>(position.z)}, g_IFaceList->m_IfListFlags & (PREORDER_CAPTURE | ORDERING_MODE)};
}
void Recording::command(const char *name, const Input &before, const wchar_t *element) {
    stream_ << "{\"type\":\"command\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
            << ",\"name\":\"" << name << "\",\"target\":5,\"rng_draws_before\":" << before.draws
            << ",\"rng_draws_after\":" << random::draw_count() << ",\"trace_position\":["
            << before.position[0] << ',' << before.position[1] << ',' << before.position[2]
            << "],\"preorder_before\":" << before.preorder << ",\"preorder_after\":"
            << (g_IFaceList->m_IfListFlags & (PREORDER_CAPTURE | ORDERING_MODE));
    const std::string_view action(name);
    if (action == "capture-key") stream_ << ",\"key\":75,\"pressed\":[true,false]";
    else if (action == "capture-button") {
        MG_CHECK(element != nullptr);
        stream_ << ",\"element\":\"";
        for (auto *value = element; *value; ++value) {
            MG_CHECK(*value >= 32 && *value <= 126 && *value != '"' && *value != '\\');
            stream_ << char(*value);
        }
        stream_ << '"';
    } else stream_ << ",\"button\":\"" << (action == "capture-context" ? "right" : "left")
                   << "\",\"client\":[0,0]";
    stream_ << "}\n";
}
void Recording::state(const char *phase, std::array<int, 3> progress) {
    MG_CHECK(!finished_);
    const auto body = capture_observation(session_, building_, roles_, phase, progress);
    stream_ << "{\"type\":\"snapshot\"," << body.substr(1) << '\n';
}
void Recording::finish() {
    MG_CHECK(!finished_);
    stream_.flush();
    stream_.close();
    pending.push_back({path_, session_.frame(), session_.time(), random::draw_count()});
    finished_ = true;
}
void complete_after_cleanup() {
    for (const auto &item : pending) {
        std::ofstream file;
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.open(item.path, std::ios::app);
        file << "{\"type\":\"complete\",\"frame\":" << item.frame << ",\"time_ms\":" << item.time
             << ",\"rng_draws\":" << item.draws << "}\n";
        file.close();
    }
    pending.clear();
}
} // namespace tests::capture_completion
