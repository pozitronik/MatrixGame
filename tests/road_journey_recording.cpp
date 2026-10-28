// MatrixGame - licensed under GPLv2 or any later version.
#include "road_journey_recording.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"

#include <bit>
#include <sstream>
#include <utility>

namespace tests::road {
namespace {
struct Pending {
    std::filesystem::path path;
    std::string tail;
};
std::vector<Pending> pending;

std::filesystem::path path(random::Mode mode, int step, const char *variant, int replay) {
    return std::filesystem::path("scenario-traces") /
        (std::string("road-journey-") + (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" +
         std::to_string(step) + "-" + variant + "-" + std::to_string(replay) + ".jsonl");
}

std::string quoted(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char value : text) {
        if (value == '"' || value == '\\') out << '\\' << char(value);
        else if (value == '\n') out << "\\n";
        else if (value == '\r') out << "\\r";
        else if (value == '\t') out << "\\t";
        else {
            MG_CHECK(value >= 32 && value < 127);
            out << char(value);
        }
    }
    out << '"';
    return out.str();
}

int identity(const battle::Session &session, const CMatrixMapStatic *object, const CMatrixRobotAI *blocker) {
    if (!object) return 0;
    for (unsigned id = 1; id <= 4; ++id)
        if (session.matches(id, reinterpret_cast<const CMatrixRobotAI *>(object))) return int(id);
    return object == blocker ? 5 : -1;
}

void queue(std::ostream &out, CMatrixRobotAI &robot, const battle::Session &session,
           const CMatrixRobotAI *blocker) {
    out << '[';
    for (int i = 0; i < robot.GetOrdersInPool(); ++i) {
        if (i) out << ',';
        const auto &order = *robot.GetOrder(i);
        out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
            << identity(session, order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr, blocker) << ']';
    }
    out << ']';
}
} // namespace

void invalidate(random::Mode mode, int step, bool obstruction) {
    std::filesystem::create_directories("scenario-traces");
    const std::array<const char *, 2> variants = obstruction
        ? std::array<const char *, 2>{"live-blocker", "destroyed-blocker"}
        : std::array<const char *, 2>{"unguided", "guided"};
    for (const auto *variant : variants) {
        for (int replay = 0; replay < 2; ++replay) {
            std::ofstream output(path(mode, step, variant, replay), std::ios::trunc);
            MG_CHECK(output.good());
        }
    }
}

Recording::Recording(const battle::Session &session, random::Mode mode, int step, const char *variant, int replay)
    : session_(session), path_(path(mode, step, variant, replay)) {
    auto header = session.metadata("road-ring-v1");
    output_.open(path_, std::ios::trunc);
    MG_CHECK(output_.good());
    header.pop_back();
    output_ << header << ",\"road_observation\":\"road-journey-v1\",\"variant\":\"" << variant
        << "\",\"journey_ms\":40000,\"hit_ms\":6000,\"settled_ms\":35000,\"trace\":"
#ifdef _TRACE
        << "true"
#else
        << "false"
#endif
        << "}\n";
    MG_CHECK(output_.good());
}

void Recording::command(const char *name, std::uint64_t before, std::array<std::uint32_t, 3> point) {
    output_ << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session_.frame()
        << ",\"time_ms\":" << session_.time() << ",\"rng_draws_before\":" << before
        << ",\"rng_draws_after\":" << random::draw_count();
    if (std::string_view(name) == "ground-move") output_ << ",\"point\":[500,280]";
    else if (std::string_view(name) == "regional-guidance") output_ << ",\"regions\":[0,1],\"chassis_mask\":2";
    else {
        MG_CHECK(std::string_view(name) == "lethal-laser-hit");
        output_ << ",\"weapon\":" << int(WEAPON_LASER)
            << ",\"attacker_side\":2,\"reported_death\":true,\"origin_bit_patterns\":["
            << point[0] << ',' << point[1] << ',' << point[2]
            << "],\"direction_bit_patterns\":[1065353216,0,0]";
    }
    output_ << "}\n";
    MG_CHECK(output_.good());
}

void Recording::snapshot(const std::string &original, const SMatrixPlayerGroup &group, SObjectCore *core,
                         CMatrixRobotAI *blocker, const Metrics &metrics, bool source_observation) {
    const auto before = random::draw_count();
    const auto end = original.find(":blocker=");
    const auto state_end = end == std::string::npos ? original.find(":enemy_kills=") : end;
    MG_CHECK(state_end != std::string::npos);
    output_ << "{\"type\":\"snapshot\",\"state\":" << original.substr(0, state_end)
        << ",\"source_observation\":" << (source_observation ? "true" : "false")
        << ",\"original_observation\":" << quoted(original) << ",\"group\":{\"order\":" << int(group.Order())
        << ",\"destination\":[" << group.m_To.x << ',' << group.m_To.y << "],\"roads\":[";
    const auto &route = *group.m_RoadPath;
    for (int list = 0, offset = 0; list < route.m_ListCnt; ++list) {
        if (list) output_ << ',';
        const auto &header = route.m_Header[list];
        output_ << "{\"distance\":" << header.m_Dist << ",\"units\":[";
        for (int i = 0; i < header.m_Cnt; ++i) {
            if (i) output_ << ',';
            const auto &unit = route.m_Units[offset++];
            output_ << "{\"zone\":" << unit.m_Crotch->m_Zone << ",\"has_road\":"
                << (unit.m_Road ? "true" : "false") << ",\"road_zones\":[";
            if (unit.m_Road) {
                for (int z = 0; z < unit.m_Road->m_ZoneCnt; ++z) {
                    if (z) output_ << ',';
                    output_ << unit.m_Road->m_Zone[z];
                }
            }
            output_ << "]}";
        }
        output_ << "]}";
    }
    output_ << "]},\"actors\":[";
    for (unsigned id = 1; id <= 4; ++id) {
        CMatrixRobotAI *robot = nullptr;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (object->IsLiveRobot() && session_.matches(id, object->AsRobot())) {
                robot = object->AsRobot();
                break;
            }
        }
        MG_CHECK(robot);
        if (id != 1) output_ << ',';
        const int x = robot->GetMapPosX(), y = robot->GetMapPosY();
        const int zone = g_MatrixMap->MoveGet(x, y)->m_Zone;
        MG_CHECK(zone >= 0 && zone < 16);
        const auto &environment = *robot->GetEnv();
        output_ << "{\"id\":" << id << ",\"map\":[" << x << ',' << y << "],\"zone\":" << zone
            << ",\"region\":" << g_MatrixMap->m_RN.m_Zone[zone].m_Region
            << ",\"place\":" << environment.m_Place << ",\"place_add\":[" << environment.m_PlaceAdd.x
            << ',' << environment.m_PlaceAdd.y << "],\"queue\":";
        queue(output_, *robot, session_, blocker);
        output_ << '}';
    }
    const auto *object = core ? core->m_Object : nullptr;
    output_ << "],\"blocker\":{\"configured\":" << (core ? "true" : "false")
        << ",\"present\":" << (object ? "true" : "false");
    if (object) {
        MG_CHECK(object == blocker);
        // State, position and health are the original observer's post-death fields.
        output_ << ",\"id\":5,\"same_created_object\":true,\"alive\":"
            << (blocker->IsLiveRobot() ? "true" : "false") << ",\"state\":" << int(blocker->m_CurrState)
            << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(blocker->m_PosX)
            << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(blocker->m_PosY)
            << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(blocker->GetHitPoint());
        if (blocker->IsLiveRobot()) {
            output_ << ",\"queue\":";
            queue(output_, *blocker, session_, blocker);
        }
    }
    output_ << "},\"enemy_kills\":" << g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL)
        << ",\"player_kills\":" << g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL)
        << ",\"progress\":{\"zones\":[" << metrics.zones[0] << ',' << metrics.zones[1]
        << "],\"crossed\":[" << metrics.crossed[0] << ',' << metrics.crossed[1]
        << "],\"arrived\":[" << metrics.arrived[0] << ',' << metrics.arrived[1]
        << "],\"minimum_separation_bits\":" << std::bit_cast<std::uint32_t>(metrics.minimum_separation)
        << ",\"retired\":" << metrics.retired << "}}\n";
    MG_CHECK(output_.good() && random::draw_count() == before);
}

void Recording::finish(const std::vector<std::string> &trace) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto &line : trace) {
        for (const unsigned char value : line) {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        hash ^= '\n';
        hash *= 1099511628211ull;
    }
    std::ostringstream tail;
    tail << "{\"type\":\"complete\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
        << ",\"rng_draws\":" << random::draw_count() << ",\"original_frames\":" << trace.size()
        << ",\"original_digest_low\":" << std::uint32_t(hash)
        << ",\"original_digest_high\":" << std::uint32_t(hash >> 32) << "}\n";
    output_.close();
    MG_CHECK(output_.good());
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
} // namespace tests::road
