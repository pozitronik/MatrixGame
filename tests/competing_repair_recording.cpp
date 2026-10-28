// MatrixGame - licensed under GPLv2 or any later version.
#include "competing_repair_recording.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Logic/MatrixEnvironment.h"
#include <bit>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tests::ai::competing_repair {
namespace {
struct Completion {
    std::string path;
    int frame, time;
    std::uint64_t draws;
};
std::vector<Completion> pending;

std::string path(random::Mode mode, int step, bool replacement, bool in_range) {
    return std::string("scenario-traces/repair-competing-") +
           (replacement ? (in_range ? "replace-near" : "replace-far") : "retirement") + "-" +
           (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" + std::to_string(step) + ".jsonl";
}
} // namespace

void invalidate(bool replacement) {
    std::filesystem::create_directories("scenario-traces");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int step : {10, 16}) {
            for (bool in_range : {false, true}) {
                if (!replacement && in_range) continue;
                std::ofstream file;
                file.exceptions(std::ios::badbit | std::ios::failbit);
                file.open(path(mode, step, replacement, in_range), std::ios::trunc);
                file.close();
            }
        }
    }
}

Recording::Recording(battle::Session &session, random::Mode mode, int step, bool replacement, bool in_range,
                     CMatrixRobotAI *repairer, CMatrixRobotAI *&first, CMatrixRobotAI *second, SObjectCore *first_core)
    : session_(session), repairer_(repairer), first_(first), second_(second), first_core_(first_core),
      path_(path(mode, step, replacement, in_range)) {
    stream_.rdbuf()->pubsetbuf(buffer_.data(), buffer_.size());
    stream_.exceptions(std::ios::badbit | std::ios::failbit);
    stream_.open(path_, std::ios::trunc);
    auto header = session.metadata("competing-moving-repair-v2");
    header.pop_back();
    stream_ << header << ",\"replacement\":" << (replacement ? "true" : "false")
            << ",\"competitor_in_range\":" << (in_range ? "true" : "false")
#ifdef _TRACE
            << ",\"trace\":true}\n";
#else
            << ",\"trace\":false}\n";
#endif
}

std::array<CMatrixRobotAI *, 6> Recording::actors() const {
    std::array<CMatrixRobotAI *, 6> result{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        for (unsigned id = 1; id <= 4; ++id)
            if (session_.matches(id, object->AsRobot())) result[id - 1] = object->AsRobot();
        if (object == first_) result[4] = object->AsRobot();
        if (object == second_) result[5] = object->AsRobot();
    }
    return result;
}

int Recording::identity(CMatrixMapStatic *object) const {
    if (!object) return 0;
    const auto current = actors();
    for (unsigned index = 0; index < current.size(); ++index)
        if (current[index] == object) return int(index + 1);
    return -1;
}

void Recording::state(const char *phase, int first_pulses, int second_pulses, float first_health, float second_health,
                      bool changed, int tick_target) {
    MG_CHECK(!finished_);
    const auto draws = random::draw_count();
    const auto current = actors();
    // A cache is local to this observation: commands can change state at the same frame.
    const auto identify = [&](CMatrixMapStatic *object) {
        if (!object) return 0;
        for (unsigned index = 0; index < current.size(); ++index)
            if (current[index] == object) return int(index + 1);
        return -1;
    };
    std::ostringstream out;
    out << "{\"type\":\"snapshot\",\"phase\":\"" << phase << "\",\"state\":" << session_.snapshot()
        << ",\"repairer\":" << identify(repairer_) << ",\"target\":" << identify(repairer_->GetEnv()->m_Target)
        << ",\"tick_target\":" << tick_target << ",\"changed\":" << (changed ? "true" : "false")
        << ",\"first_core\":" << identify(first_core_->m_Object) << ",\"pulse_counts\":["
        << first_pulses << ',' << second_pulses << "],\"cached_health\":["
        << std::bit_cast<uint32_t>(first_health) << ',' << std::bit_cast<uint32_t>(second_health)
        << "],\"logic_order\":[";
    bool emitted = false;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (emitted) out << ',';
        out << identify(object);
        emitted = true;
    }
    out << "],\"actors\":[";
    for (unsigned index = 0; index < current.size(); ++index) {
        if (index) out << ',';
        auto *robot = current[index];
        out << "{\"id\":" << index + 1 << ",\"present\":" << (robot ? "true" : "false");
        if (robot) {
            const bool alive = robot->IsLiveRobot();
            out << ",\"alive\":" << (alive ? "true" : "false") << ",\"state\":" << int(robot->m_CurrState);
            // A non-live robot may reuse its union storage. Never inspect live-only fields then.
            if (alive) {
                out << ",\"x_bits\":" << std::bit_cast<uint32_t>(robot->m_PosX)
                    << ",\"y_bits\":" << std::bit_cast<uint32_t>(robot->m_PosY)
                    << ",\"health_bits\":" << std::bit_cast<uint32_t>(robot->GetHitPoint())
                    << ",\"group\":" << robot->GetGroupLogic() << ",\"place\":" << robot->GetEnv()->m_Place
                    << ",\"in_place\":" << (robot->PLIsInPlace() ? "true" : "false") << ",\"order\":";
                const int slot = robot->GetGroupLogic();
                MG_CHECK(slot >= -1 && slot < MAX_LOGIC_GROUP);
                auto *group = slot >= 0 && robot->m_Side == PLAYER_SIDE ?
                    &g_MatrixMap->GetPlayerSide()->m_PlayerGroup[slot] : nullptr;
                out << (group ? int(group->Order()) : -1) << ",\"destination\":["
                    << (group ? group->m_To.x : -1) << ',' << (group ? group->m_To.y : -1)
                    << "],\"queue\":[";
                const int count = robot->GetOrdersInPool();
                MG_CHECK(count >= 0 && count <= MAX_ORDERS);
                for (int order_index = 0; order_index < count; ++order_index) {
                    if (order_index) out << ',';
                    const auto &order = *robot->GetOrder(order_index);
                    // Read the retained core directly; GetStatic() can mutate a stale reference.
                    out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                        << std::bit_cast<uint32_t>(order.m_Param1) << ','
                        << std::bit_cast<uint32_t>(order.m_Param2) << ','
                        << std::bit_cast<uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                        << identify(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
                }
                out << ']';
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
    if (std::string_view(name) == "move") stream_ << ",\"world\":[500,200]";
    else if (std::string_view(name) == "replace-move") stream_ << ",\"world\":[300,500]";
    else stream_ << ",\"patient\":5";
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
} // namespace tests::ai::competing_repair
