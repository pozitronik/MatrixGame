// MatrixGame - licensed under GPLv2 or any later version.
#include "command_lifecycle_recording.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "Interface/CInterface.h"
#include <algorithm>
#include <array>
#include <bit>
#include <sstream>
#include <utility>

namespace tests::c2 {
namespace {
Recording *active = nullptr;
std::vector<std::pair<std::filesystem::path, std::string>> pending;

std::filesystem::path path(const char *family, random::Mode mode, int step, int route, bool flag) {
    std::string name = std::string("player-command-") + family + '-' +
        (mode == random::Mode::ParkMiller ? "pm-" : "crt-") + std::to_string(step);
    if (route >= 0) {
        name += std::string("-") + (std::string(family) == "mixed" ? (flag ? "button" : "key") :
            (flag ? "stop" : "keep")) + "-route" + std::to_string(route);
    }
    return std::filesystem::current_path() / "scenario-traces" / (name + ".jsonl");
}

void truncate(const std::filesystem::path &name) {
    std::ofstream file(name, std::ios::trunc);
    MG_CHECK(file.good());
}
}

void invalidate(const char *family) {
    MG_CHECK(active == nullptr && pending.empty());
    std::filesystem::create_directories(std::filesystem::current_path() / "scenario-traces");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) {
            if (std::string(family) == "dying") truncate(path(family, mode, step, -1, false));
            else for (int route = 0; route < 3; ++route) for (bool flag : {false, true})
                truncate(path(family, mode, step, route, flag));
        }
}

Recording::Recording(battle::Session &session, const char *family, random::Mode mode, int step, int route, bool flag)
    : session_(session), path_(path(family, mode, step, route, flag)), file_(path_, std::ios::trunc) {
    MG_CHECK(active == nullptr && file_.good());
    for (unsigned id = 1; id <= 4; ++id) {
        CMatrixRobotAI *found = nullptr;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsRobot() && session.matches(id, object->AsRobot())) found = object->AsRobot();
        MG_CHECK(found);
        actors_.push_back(found);
        uids_.push_back(found->UID);
    }
    auto header = session.metadata(std::string(family) == "spawning" ? "spawn-command-lifecycle-v1" :
        std::string(family) == "mixed" ? "mixed-command-lifecycle-v1" :
        std::string(family) == "carried" ? "carried-command-lifecycle-v1" : "dying-command-lifecycle-v1");
    header.pop_back();
#ifdef _TRACE
    constexpr bool traced = true;
#else
    constexpr bool traced = false;
#endif
    file_ << header << ",\"c2_observation\":\"command-lifecycle-v1\",\"family\":\"" << family
          << "\",\"move_route\":" << route << ",\"flag\":" << (flag ? "true" : "false")
          << ",\"trace\":" << (traced ? "true" : "false")
          << ",\"extra_actor\":" << (std::string(family) == "dying" ? 0 : 5) << "}\n";
    MG_CHECK(file_.good());
    state("session-ready");
    active = this;
}

Recording::~Recording() { active = nullptr; }

void Recording::extra(CMatrixRobotAI *robot) {
    MG_CHECK(actors_.size() == 4 && robot);
    actors_.push_back(robot);
    uids_.push_back(robot->UID);
    state("extra-created");
}

CMatrixRobotAI *Recording::present(unsigned index) const {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object == actors_[index] && object->IsRobot() && object->AsRobot()->UID == uids_[index])
            return object->AsRobot();
    return nullptr;
}

int Recording::id(const void *object) const {
    if (!object) return 0;
    for (unsigned i = 0; i < actors_.size(); ++i)
        if (actors_[i] == object && present(i) == object) return int(i + 1);
    return -1;
}

void Recording::state(const char *phase) {
    const auto draws = random::draw_count();
    std::array<CMatrixRobotAI *, 5> observed{};
    std::array<int, MAX_LOGIC_GROUP> members{};
    for (unsigned index = 0; index < actors_.size(); ++index) {
        auto *robot = present(index);
        observed[index] = robot;
        if (robot && robot->m_Side == PLAYER_SIDE && robot->GetGroupLogic() >= 0) {
            MG_CHECK(robot->GetGroupLogic() < MAX_LOGIC_GROUP);
            ++members[robot->GetGroupLogic()];
        }
    }
    std::ostringstream out;
    out << "{\"type\":\"snapshot\",\"state\":" << session_.snapshot()
        << ",\"c2\":{\"phase\":\"" << phase << "\",\"actors\":[";
    for (unsigned index = 0; index < actors_.size(); ++index) {
        if (index) out << ',';
        auto *robot = observed[index];
        out << "{\"id\":" << index + 1 << ",\"present\":" << (robot ? "true" : "false");
        if (robot) {
            const bool alive = robot->IsLiveRobot();
            out << ",\"alive\":" << (alive ? "true" : "false")
                << ",\"state\":" << int(robot->m_CurrState);
            // Death repurposes unit storage for debris; only its lifetime state remains relevant here.
            if (alive) {
                out << ",\"chassis\":" << robot->m_Unit[0].u1.s1.m_Kind
                    << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosX)
                    << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosY)
                    << ",\"group\":" << robot->GetGroupLogic() << ",\"place\":" << robot->GetEnv()->m_Place
                    << ",\"map\":[" << robot->GetMapPosX() << ',' << robot->GetMapPosY() << ']'
                    << ",\"in_place\":" << (robot->PLIsInPlace() ? "true" : "false")
                    << ",\"has_base\":" << (robot->GetBase() ? "true" : "false")
                    << ",\"maximum_speed_bits\":" << std::bit_cast<std::uint32_t>(robot->GetMaxSpeed())
                    << ",\"move_destination\":";
                CPoint destination;
                if (robot->GetMoveToCoords(destination)) out << '[' << destination.x << ',' << destination.y << ']';
                else out << "null";
                out << ",\"queue\":[";
                const int orders = robot->GetOrdersInPool();
                MG_CHECK(orders >= 0 && orders <= MAX_ORDERS);
                for (int i = 0; i < orders; ++i) {
                    if (i) out << ',';
                    const auto &order = *robot->GetOrder(i);
                    out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                        << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
                        << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
                        << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                        << id(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
                }
                out << ']';
            }
        }
        out << '}';
    }
    out << "],\"selection\":[";
    auto &side = *g_MatrixMap->GetPlayerSide();
    if (auto *group = side.GetCurGroup()) for (int i = 0; i < group->GetObjectsCnt(); ++i) {
        if (i) out << ',';
        out << id(group->GetObjectByN(i));
    }
    out << "],\"groups\":[";
    bool first = true;
    for (int slot = 0; slot < MAX_LOGIC_GROUP; ++slot) {
        const auto &group = side.m_PlayerGroup[slot];
        if (!members[slot] && !group.m_RobotCnt) continue;
        out << (first ? "" : ",") << "{\"slot\":" << slot << ",\"stored_count\":" << group.m_RobotCnt
            << ",\"members\":" << members[slot] << ",\"order\":" << int(group.Order())
            << ",\"destination\":[" << group.m_To.x << ',' << group.m_To.y << "],\"target\":" << id(group.m_Obj) << '}';
        first = false;
    }
    CMatrixFlyer *carrier = nullptr;
    for (int i = 0; i < g_MatrixMap->m_AD_Obj_cnt; ++i)
        if (g_MatrixMap->m_AD_Obj[i] == carrier_ && carrier_ && g_MatrixMap->m_AD_Obj[i]->IsFlyer())
            carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[i]);
    out << "],\"carrier_present\":" << (carrier ? "true" : "false")
        << ",\"carried_actor\":" << (carrier ? id(carrier->GetCarryingRobot()) : 0)
        << ",\"watched_core\":" << (core_ ? id(core_->m_Object) : 0)
        << ",\"enemy_kills\":" << g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) << "}}";
    MG_CHECK(random::draw_count() == draws);
    file_ << out.str() << '\n';
    MG_CHECK(file_.good());
}

void Recording::command(const char *name, std::uint64_t draws_before) {
    file_ << "{\"type\":\"command\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
          << ",\"name\":\"" << name << "\",\"rng_draws_before\":" << draws_before
          << ",\"rng_draws_after\":" << random::draw_count() << "}\n";
    MG_CHECK(file_.good());
    state(name);
}

void Recording::finish(int intermediate, int ready) {
    state("final");
    std::ostringstream out;
    out << "{\"type\":\"complete\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
        << ",\"c2\":{\"intermediate_ms\":" << intermediate << ",\"ready_ms\":" << ready << "}}\n";
    file_.flush();
    MG_CHECK(file_.good());
    pending.emplace_back(path_, out.str());
}

void tick(battle::Session &session) {
    session.advance();
    if (active) active->state("tick");
}

void command(const char *name, std::uint64_t draws_before) {
    if (active) active->command(name, draws_before);
}

void observe(const char *phase) {
    if (active) active->state(phase);
}

void complete_after_cleanup() {
    MG_CHECK(active == nullptr);
    for (const auto &[name, value] : pending) {
        std::ofstream file(name, std::ios::app);
        MG_CHECK(file.good());
        file << value;
        file.flush();
        MG_CHECK(file.good());
    }
    pending.clear();
}

} // namespace tests::c2
