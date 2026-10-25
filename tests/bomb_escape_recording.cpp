// MatrixGame - licensed under GPLv2 or any later version.
#include "bomb_escape_recording.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Logic/MatrixEnvironment.h"
#include <bit>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <vector>

namespace tests::battle::bomb_escape {
namespace {
struct Completion {
    std::string path;
    int frame, time;
    std::uint64_t draws;
};
std::vector<Completion> pending;

std::string path(std::string_view refuge, bool detonate, bool original, random::Mode mode, int step, int replay) {
    return "scenario-traces/bomb-escape-" + std::string(refuge) + "-" + (detonate ? "detonate" : "remove") + "-" +
           (original ? "original" : "corrected") + "-" + (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" +
           std::to_string(step) + "-" + std::to_string(replay) + ".jsonl";
}
} // namespace

void invalidate(std::string_view refuge) {
    std::filesystem::create_directories("scenario-traces");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16})
            for (bool original : {false, true})
                for (bool detonate : {false, true})
                    for (int replay : {0, 1}) {
                        std::ofstream file;
                        file.exceptions(std::ios::badbit | std::ios::failbit);
                        file.open(path(refuge, detonate, original, mode, step, replay), std::ios::trunc);
                        file.close();
                    }
}

Recording::Recording(Session &session, std::string_view refuge, bool detonate, bool original, random::Mode mode,
                     int step, int replay, SObjectCore *bomber_core)
    : session_(session), bomber_core_(bomber_core), bomber_identity_(bomber_core->m_Object),
      path_(path(refuge, detonate, original, mode, step, replay)) {
    stream_.rdbuf()->pubsetbuf(buffer_.data(), buffer_.size());
    stream_.exceptions(std::ios::badbit | std::ios::failbit);
    stream_.open(path_, std::ios::trunc);
    auto header = session.metadata("bomb-escape-journey-v1");
    header.pop_back();
    stream_ << header << ",\"refuge\":\"" << refuge << "\",\"detonate\":" << (detonate ? "true" : "false")
            << ",\"replay\":" << replay
#ifdef _TRACE
            << ",\"trace\":true}\n";
#else
            << ",\"trace\":false}\n";
#endif
}

void Recording::state(const char *phase, std::string_view raw, const Milestones &milestones) {
    MG_CHECK(!finished_);
    const auto draws = random::draw_count();
    std::array<CMatrixRobotAI *, 4> actors{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        for (unsigned id = 1; id <= 4; ++id)
            if (session_.matches(id, object->AsRobot())) actors[id - 1] = object->AsRobot();
    }
    const auto identity = [&](CMatrixMapStatic *object) {
        if (!object) return 0;
        for (unsigned index = 0; index < actors.size(); ++index)
            if (actors[index] == object) return int(index + 1);
        return -1;
    };
    std::string_view prefix;
    for (auto candidate : {std::string_view("remove:"), std::string_view("detonate:")})
        if (raw.starts_with(candidate)) {
            prefix = candidate;
            raw.remove_prefix(candidate.size());
            break;
        }
    std::ostringstream out;
    out << "{\"type\":\"snapshot\",\"phase\":\"" << phase << "\",\"original_prefix\":\"" << prefix
        << "\",\"state\":" << raw << ",\"bomber_core_present\":" << (bomber_core_->m_Object ? "true" : "false")
        << ",\"bomber_core\":" << (bomber_core_->m_Object ?
             (bomber_core_->m_Object == bomber_identity_ ? 3 : -1) : 0)
        << ",\"milestones\":{\"safe\":" << milestones.safe << ",\"refuge\":" << milestones.refuge
        << ",\"first_return\":" << milestones.first_return << ",\"second_escape\":" << milestones.second_escape
        << ",\"cleared\":" << milestones.cleared << ",\"damage\":" << milestones.damage
        << ",\"arrived\":" << milestones.arrived << ",\"retired\":" << milestones.retired
        << ",\"maximum_distance_bits\":" << std::bit_cast<uint32_t>(milestones.maximum_distance)
        << "},\"kills\":[" << g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) << ','
        << g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) << "],\"start_edge_masks\":[";
    const auto &place = g_MatrixMap->m_RN.m_Place[0];
    for (int index = 0; index < place.m_NearCnt; ++index) {
        if (index) out << ',';
        out << int(place.m_NearMove[index]);
    }
    out << "],\"actors\":[";
    for (unsigned index = 0; index < actors.size(); ++index) {
        if (index) out << ',';
        auto *robot = actors[index];
        out << "{\"id\":" << index + 1 << ",\"present\":" << (robot ? "true" : "false");
        if (robot) {
            const bool alive = robot->IsLiveRobot();
            out << ",\"alive\":" << (alive ? "true" : "false") << ",\"state\":" << int(robot->m_CurrState);
            // A dying robot may reuse union storage; its live-only payload is deliberately absent.
            if (alive) {
                CPoint move(-1, -1), saved(-1, -1);
                const bool moving = robot->GetMoveToCoords(move), returning = robot->GetReturnCoords(saved);
                out << ",\"x_bits\":" << std::bit_cast<uint32_t>(robot->m_PosX)
                    << ",\"y_bits\":" << std::bit_cast<uint32_t>(robot->m_PosY)
                    << ",\"health_bits\":" << std::bit_cast<uint32_t>(robot->GetHitPoint())
                    << ",\"group\":" << robot->GetGroupLogic() << ",\"place\":" << robot->GetEnv()->m_Place
                    << ",\"moving\":" << (moving ? "true" : "false")
                    << ",\"move\":[" << move.x << ',' << move.y << "],\"returning\":"
                    << (returning ? "true" : "false") << ",\"saved\":[" << saved.x << ',' << saved.y
                    << "],\"must_die\":" << (robot->IsMustDie() ? "true" : "false") << ",\"queue\":[";
                const int count = robot->GetOrdersInPool();
                MG_CHECK(count >= 0 && count <= MAX_ORDERS);
                for (int order_index = 0; order_index < count; ++order_index) {
                    if (order_index) out << ',';
                    const auto &order = *robot->GetOrder(order_index);
                    out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                        << std::bit_cast<uint32_t>(order.m_Param1) << ','
                        << std::bit_cast<uint32_t>(order.m_Param2) << ','
                        << std::bit_cast<uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                        << identity(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
                }
                bool observes_cover = false;
                if (index == 2)
                    for (auto *enemy = robot->GetEnv()->m_FirstEnemy; enemy; enemy = enemy->m_NextEnemy)
                        if (enemy->m_Enemy == actors[0]) observes_cover = true;
                out << "],\"observes_cover\":" << (observes_cover ? "true" : "false");
            }
        }
        out << '}';
    }
    out << "]}\n";
    MG_CHECK(random::draw_count() == draws);
    stream_ << out.str();
}

void Recording::command(const char *name, std::uint64_t before) {
    stream_ << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session_.frame()
            << ",\"time_ms\":" << session_.time() << ",\"rng_draws_before\":" << before
            << ",\"rng_draws_after\":" << random::draw_count();
    const std::string_view action(name);
    if (action == "reserve-cover") stream_ << ",\"actor\":1,\"destination\":[48,8]";
    else if (action == "initial-move") stream_ << ",\"actor\":2,\"destination\":[20,20]";
    else if (action == "escape-from-bomb") stream_ << ",\"side\":1";
    else if (action == "block-refuge-edges") {
        stream_ << ",\"actor\":2,\"masks\":[";
        const auto &place = g_MatrixMap->m_RN.m_Place[0];
        for (int index = 0; index < place.m_NearCnt; ++index) {
            if (index) stream_ << ',';
            stream_ << int(place.m_NearMove[index]);
        }
        stream_ << ']';
    } else stream_ << ",\"actor\":3";
    stream_ << "}\n";
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
} // namespace tests::battle::bomb_escape
