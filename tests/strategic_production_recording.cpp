// MatrixGame - licensed under GPLv2 or any later version.
#include "strategic_production_recording.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"
#include <bit>
#include <sstream>
#include <vector>

namespace tests::production {
namespace {
struct Pending {
    std::filesystem::path path;
    std::string tail;
};
std::vector<Pending> pending;
bool listed(const CMatrixMapStatic *expected) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object == expected) return true;
    return false;
}
std::filesystem::path path(random::Mode mode, int step, int ending) {
    return std::filesystem::path("scenario-traces") / (std::string("production-strategic-") + ending_name(ending)
        + "-" + (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" + std::to_string(step) + ".jsonl");
}
int identity(const battle::Session &session, const CMatrixMapStatic *object,
             const CMatrixMapStatic *base, const CMatrixMapStatic *robot) {
    if (!object) return 0;
    for (unsigned id = 1; id <= 4; ++id)
        if (session.matches(id, reinterpret_cast<const CMatrixRobotAI *>(object))) return int(id);
    if (object == base) return 5;
    if (object == robot) return 6;
    return -1;
}
} // namespace

const char *ending_name(int ending) {
    switch (ending) {
    case 0: return "deploy";
    case 1: return "wait-base-removed";
    case 2: return "paid-base-removed";
    }
    throw std::logic_error("Unknown strategic production ending");
}

std::string observe(const battle::Session &session, const View &view, const char *phase) {
    const auto draws = random::draw_count();
    auto &side = *g_MatrixMap->GetPlayerSide();
    const auto *base_object = view.base_core->m_Object;
    MG_CHECK(!base_object || base_object == view.base);
    CMatrixMapStatic *queued = base_object ? view.base->m_BS.GetTopItem() : nullptr;
    CMatrixMapStatic *robot_object = view.robot_core ? view.robot_core->m_Object : queued;
    MG_CHECK(!robot_object || !view.robot || robot_object == view.robot);
    const bool initialized = robot_object && listed(robot_object);
    std::ostringstream out;
    out << "{\"state\":" << session.snapshot() << ",\"phase\":\"" << phase << "\",\"resources\":[";
    for (int r = 0; r < MAX_RESOURCES; ++r) {
        if (r) out << ',';
        out << side.GetResourcesAmount(static_cast<ERes>(r));
    }
    out << "],\"strategy\":{\"waiting\":" << side.m_WaitResForBuildRobot
        << ",\"last_built\":" << side.m_BuildRobotLast << ",\"build_count\":" << side.GetStatValue(STAT_ROBOT_BUILD)
        << "},\"base\":{\"id\":5,\"present\":" << (base_object ? "true" : "false");
    if (base_object) {
        out << ",\"in_logic\":" << (listed(base_object) ? "true" : "false")
            << ",\"side\":" << view.base->GetSide() << ",\"state\":" << int(view.base->m_State)
            << ",\"floor_bits\":" << std::bit_cast<std::uint32_t>(view.base->m_BaseFloor)
            << ",\"spawning\":" << (view.base->IsSpawningBot() ? "true" : "false")
            << ",\"queue_count\":" << view.base->m_BS.GetItemsCnt() << ",\"queue\":[";
        int count = 0;
        for (auto *item = queued; item; item = item->m_NextStackItem) {
            MG_CHECK(count < MAX_STACK_UNITS);
            if (count++) out << ',';
            out << identity(session, item, base_object, robot_object);
        }
        MG_CHECK(count == view.base->m_BS.GetItemsCnt());
        out << ']';
    }
    out << "},\"robot\":{\"id\":6,\"created\":" << (view.robot || queued ? "true" : "false")
        << ",\"present\":" << (robot_object ? "true" : "false")
        << ",\"initialized\":" << (initialized ? "true" : "false")
        << ",\"in_queue\":" << (robot_object && queued == robot_object ? "true" : "false")
        << ",\"in_logic\":" << (initialized ? "true" : "false");
    if (initialized) {
        auto *robot = static_cast<CMatrixRobotAI *>(robot_object);
        const bool alive = robot->IsLiveRobot();
        out << ",\"alive\":" << (alive ? "true" : "false") << ",\"state\":" << int(robot->m_CurrState);
        if (alive) {
            const int count = robot->GetOrdersInPool();
            MG_CHECK(count >= 0 && count <= MAX_ORDERS);
            out << ",\"side\":" << robot->GetSide() << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosX)
                << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosY)
                << ",\"base\":" << identity(session, robot->GetBase(), base_object, robot_object)
                << ",\"manual_disabled\":" << (robot->IsDisableManual() ? "true" : "false") << ",\"queue\":[";
            for (int i = 0; i < count; ++i) {
                if (i) out << ',';
                const auto &order = *robot->GetOrder(i);
                out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                    << identity(session, order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr,
                                base_object, robot_object) << ']';
            }
            out << ']';
        }
    }
    out << "},\"progress\":{\"payouts\":" << view.payouts << ",\"paid\":" << view.paid
        << ",\"spawned\":" << view.spawned << ",\"opened\":" << view.opened
        << ",\"detached\":" << view.detached << ",\"closed\":" << view.closed << '}';
    if (view.capacity) out << ",\"capacity_sample\":" << *view.capacity;
    if (view.constructor_price && view.weapon_samples) {
        out << ",\"assembly\":{\"price\":[";
        for (int i = 0; i < 4; ++i) {
            if (i) out << ',';
            out << (*view.constructor_price)[i];
        }
        out << "],\"weapons\":[";
        for (int i = 0; i < 2; ++i) {
            if (i) out << ',';
            const auto &sample = (*view.weapon_samples)[i];
            out << '[' << sample[0] << ',' << sample[1] << ',' << sample[2] << ']';
        }
        out << "]}";
    }
    if (view.detach_distance)
        out << ",\"detach_distance_squared_bits\":" << std::bit_cast<std::uint32_t>(*view.detach_distance);
    out << '}';
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}

void invalidate(int ending) {
    std::filesystem::create_directories("scenario-traces");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        std::ofstream output(path(mode, step, ending), std::ios::trunc);
        output.close();
        MG_CHECK(output.good());
    }
}
Recording::Recording(const battle::Session &session, random::Mode mode, int step, int ending)
    : session_(session), path_(path(mode, step, ending)), output_(path_, std::ios::trunc) {
    MG_CHECK(output_.good());
    auto metadata = session.metadata("synthetic-strategic-purchase-v1");
    metadata.pop_back();
    output_ << metadata << ",\"production_observation\":\"strategic-production-v1\",\"ending\":\""
        << ending_name(ending) << "\",\"navigation_layout\":\"central-obstacle-v1\",\"trace\":"
#ifdef _TRACE
        << "true"
#else
        << "false"
#endif
        << "}\n";
}
void Recording::command(const char *name, std::uint64_t before) {
    output_ << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session_.frame()
        << ",\"time_ms\":" << session_.time() << ",\"rng_draws_before\":" << before
        << ",\"rng_draws_after\":" << random::draw_count() << "}\n";
    MG_CHECK(output_.good());
}
void Recording::snapshot(const std::string &payload) {
    MG_CHECK(!payload.empty() && payload.front() == '{');
    output_ << "{\"type\":\"snapshot\"," << payload.substr(1) << '\n';
    for (unsigned char value : payload) {
        hash_ ^= value;
        hash_ *= 1099511628211ull;
    }
    hash_ ^= '\n';
    hash_ *= 1099511628211ull;
    ++observations_;
    MG_CHECK(output_.good());
}
void Recording::finish() {
    output_.close();
    MG_CHECK(output_.good());
    std::ostringstream tail;
    tail << "{\"type\":\"complete\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
        << ",\"rng_draws\":" << random::draw_count() << ",\"observations\":" << observations_
        << ",\"observation_digest_low\":" << std::uint32_t(hash_)
        << ",\"observation_digest_high\":" << std::uint32_t(hash_ >> 32) << "}\n";
    pending.push_back({path_, tail.str()});
}
void complete_after_cleanup() {
    for (const auto &record : pending) {
        std::ofstream output(record.path, std::ios::app);
        output << record.tail;
        output.close();
        MG_CHECK(output.good());
    }
    pending.clear();
}
} // namespace tests::production
