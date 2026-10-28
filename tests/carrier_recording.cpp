// MatrixGame - licensed under GPLv2 or any later version.
#include "carrier_recording.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"

#include <bit>
#include <sstream>
#include <vector>

namespace tests::carrier {
namespace {
struct Pending { std::filesystem::path path; std::string tail; };
std::vector<Pending> pending;

std::filesystem::path path(const char *family, random::Mode mode, int step, int variant, int replay) {
    return std::filesystem::path("scenario-traces") /
        (std::string("carrier-") + family + "-" + (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" +
         std::to_string(step) + "-" + std::to_string(variant) + "-" + std::to_string(replay) + ".jsonl");
}

const char *fixture(std::string_view family) {
    if (family == "flights") return "synthetic-navigation-delivery-v1";
    if (family == "neighbors") return "neighboring-delivery-landings-v1";
    if (family == "damage") return "synthetic-navigation-carrier-damage-v1";
    MG_CHECK(family == "approach");
    return "carrier-approach-obstacle-v1";
}

void quoted(std::ostream &out, const std::string &text) {
    out << '"';
    for (unsigned char value : text) {
        if (value == '"' || value == '\\') out << '\\';
        MG_CHECK(value >= 32 && value < 127);
        out << char(value);
    }
    out << '"';
}

template<class Values>
void integers(std::ostream &out, const Values &values) {
    out << '[';
    bool first = true;
    for (auto value : values) {
        if (!first) out << ',';
        first = false;
        out << value;
    }
    out << ']';
}

template<class Values>
void float_bits(std::ostream &out, const Values &values) {
    std::array<std::uint32_t, 3> values_bits{};
    for (int i = 0; i < 3; ++i) values_bits[i] = std::bit_cast<std::uint32_t>(values[i]);
    integers(out, values_bits);
}

int identity(const battle::Session &session, const CMatrixMapStatic *object, std::span<const Pair> pairs,
             const CMatrixMapStatic *obstacle) {
    if (!object) return 0;
    for (unsigned id = 1; id <= 4; ++id)
        if (session.matches(id, reinterpret_cast<const CMatrixRobotAI *>(object))) return int(id);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].cargo_core->m_Object == object) return int(5 + 2 * i);
        if (pairs[i].carrier_core->m_Object == object) return int(6 + 2 * i);
    }
    return obstacle && object == obstacle ? 9 : -1;
}

void queue(std::ostream &out, CMatrixRobotAI &robot, const battle::Session &session,
           std::span<const Pair> pairs, const CMatrixMapStatic *obstacle) {
    out << '[';
    for (int i = 0; i < robot.GetOrdersInPool(); ++i) {
        if (i) out << ',';
        const auto &order = *robot.GetOrder(i);
        out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
            << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
            << identity(session, order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr, pairs, obstacle) << ']';
    }
    out << ']';
}
} // namespace

std::string observe(const battle::Session &session, std::span<const Pair> pairs, const Frame &frame) {
    const auto draws = random::draw_count();
    std::ostringstream out;
    out << "{\"state\":" << session.snapshot() << ",\"active_carriers\":" << g_MatrixMap->m_AD_Obj_cnt
        << ",\"rejected_icons\":" << session.rejected_icon_targets() << ",\"pairs\":[";
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const auto &pair = pairs[i];
        if (i) out << ',';
        out << "{\"request_index\":" << i << ",\"requested_place\":" << pair.requested_place
            << ",\"destination\":";
        integers(out, pair.destination);
        out << ",\"carrier\":{\"id\":" << 6 + 2 * i
            << ",\"present\":" << (pair.carrier_core->m_Object ? "true" : "false");
        if (pair.carrier_core->m_Object) {
            MG_CHECK(pair.carrier_core->m_Object == pair.carrier);
            bool listed = false;
            for (int j = 0; j < g_MatrixMap->m_AD_Obj_cnt; ++j)
                listed |= g_MatrixMap->m_AD_Obj[j] == pair.carrier;
            MG_CHECK(listed);
            out << ",\"in_active_list\":true,\"health_bits\":"
                << std::bit_cast<std::uint32_t>(pair.carrier->GetHitPoint())
                << ",\"carrying\":" << (pair.carrier->CarryingRobot() ? "true" : "false")
                << ",\"cargo\":" << identity(session, pair.carrier->GetCarryingRobot(), pairs, frame.obstacle)
                << ",\"pose_sampled\":" << (pair.carrier_pose ? "true" : "false");
            if (pair.carrier_pose) {
                out << ",\"position_bit_patterns\":";
                float_bits(out, *pair.carrier_pose);
            }
        }
        out << "},\"cargo\":{\"id\":" << 5 + 2 * i
            << ",\"present\":" << (pair.cargo_core->m_Object ? "true" : "false");
        if (pair.cargo_core->m_Object) {
            MG_CHECK(pair.cargo_core->m_Object == pair.cargo);
            const auto &robot = *pair.cargo;
            const bool alive = robot.IsLiveRobot();
            out << ",\"alive\":" << (alive ? "true" : "false") << ",\"state\":" << int(robot.m_CurrState);
            if (alive) {
                bool listed = false;
                for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                    listed |= object == pair.cargo;
                const auto &environment = *pair.cargo->GetEnv();
                const int group = pair.cargo->GetGroupLogic();
                out << ",\"in_logic_list\":" << (listed ? "true" : "false")
                    << ",\"must_die\":" << (robot.IsMustDie() ? "true" : "false")
                    << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(robot.m_PosX)
                    << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(robot.m_PosY)
                    << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(robot.GetHitPoint())
                    << ",\"in_place\":" << (robot.PLIsInPlace() ? "true" : "false")
                    << ",\"place\":" << environment.m_Place << ",\"place_add\":["
                    << environment.m_PlaceAdd.x << ',' << environment.m_PlaceAdd.y << "]"
                    << ",\"group\":" << group << ",\"group_state\":{\"present\":"
                    << (group >= 0 ? "true" : "false");
                if (group >= 0) {
                    MG_CHECK(group < MAX_LOGIC_GROUP);
                    const auto &order = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group];
                    out << ",\"order\":" << int(order.Order()) << ",\"target\":"
                        << identity(session, order.m_Obj, pairs, frame.obstacle)
                        << ",\"destination\":[" << order.m_To.x << ',' << order.m_To.y << ']';
                }
                out << "},\"queue\":";
                queue(out, *pair.cargo, session, pairs, frame.obstacle);
                out << ",\"height_sampled\":" << (pair.cargo_height ? "true" : "false");
                if (pair.cargo_height) out << ",\"height_bits\":" << std::bit_cast<std::uint32_t>(*pair.cargo_height);
            }
        }
        out << "},\"progress\":";
        if (pair.progress) {
            const auto &p = *pair.progress;
            out << "{\"release\":" << p.release << ",\"landed\":" << p.landed << ",\"retired\":" << p.retired
                << ",\"falling_samples\":" << p.falling_samples << '}';
        }
        else out << "null";
        out << '}';
    }
    out << "]";
    if (frame.resources) {
        out << ",\"resources\":";
        integers(out, *frame.resources);
    }
    if (frame.approach) {
        const auto &a = *frame.approach;
        out << ",\"approach\":{\"obstacle\":9,\"minimum_bit_patterns\":";
        float_bits(out, a.minimum);
        out << ",\"maximum_bit_patterns\":";
        float_bits(out, a.maximum);
        out << ",\"roof_bits\":" << std::bit_cast<std::uint32_t>(a.roof)
            << ",\"maximum_z_bits\":" << std::bit_cast<std::uint32_t>(a.maximum_z)
            << ",\"maximum_surface_bits\":" << std::bit_cast<std::uint32_t>(a.maximum_surface)
            << ",\"inside\":" << a.inside << ",\"first_inside\":" << a.first_inside
            << ",\"last_inside\":" << a.last_inside;
        if (a.sampled_surface) out << ",\"sampled_surface_bits\":" << std::bit_cast<std::uint32_t>(*a.sampled_surface);
        if (a.hit_roof) out << ",\"hit_roof_bits\":" << std::bit_cast<std::uint32_t>(*a.hit_roof);
        out << '}';
    }
    if (!frame.original.empty()) {
        out << ",\"original_observation\":";
        quoted(out, frame.original);
    }
    out << '}';
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}

void invalidate(const char *family, std::optional<random::Mode> selected_mode, int selected_step) {
    std::filesystem::create_directories("scenario-traces");
    const std::string_view kind(family);
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        if (selected_mode && mode != *selected_mode) continue;
        for (int step : {10, 16}) {
            if ((kind == "damage" && step != 10) || (selected_step && step != selected_step)) continue;
            const int count = kind == "flights" ? 4 : kind == "approach" ? 2 : 3;
            for (int variant = 0; variant < count; ++variant) {
                for (int replay = 0; replay < (kind == "approach" ? 2 : 1); ++replay) {
                    std::ofstream output(path(family, mode, step, variant, replay), std::ios::trunc);
                    output.close();
                    MG_CHECK(output.good());
                }
            }
        }
    }
}

Recording::Recording(const battle::Session &session, random::Mode mode, int step,
                     const char *family, int variant, int replay)
    : session_(session), path_(path(family, mode, step, variant, replay)), output_(path_, std::ios::trunc) {
    MG_CHECK(output_.good());
    auto metadata = session.metadata(fixture(family));
    metadata.pop_back();
    output_ << metadata << ",\"carrier_observation\":\"carrier-journey-v1\",\"family\":\"" << family
        << "\",\"variant\":" << variant << ",\"trace\":"
#ifdef _TRACE
        << "true"
#else
        << "false"
#endif
        << "}\n";
}

void Recording::command(const char *name, std::uint64_t before, const std::string &fields) {
    output_ << "{\"type\":\"command\",\"name\":\"" << name << "\",\"frame\":" << session_.frame()
        << ",\"time_ms\":" << session_.time() << ",\"rng_draws_before\":" << before
        << ",\"rng_draws_after\":" << random::draw_count() << fields << "}\n";
    MG_CHECK(output_.good());
}

void Recording::request(std::uint64_t before, float angle, std::array<float, 2> landing, int place,
                        std::array<int, 2> destination) {
    std::ostringstream fields;
    fields << ",\"order\":" << int(FO_GIVE_BOT) << ",\"angle_bits\":" << std::bit_cast<std::uint32_t>(angle)
        << ",\"landing_x_bits\":" << std::bit_cast<std::uint32_t>(landing[0])
        << ",\"landing_y_bits\":" << std::bit_cast<std::uint32_t>(landing[1])
        << ",\"place\":" << place << ",\"destination\":";
    integers(fields, destination);
    command("request", before, fields.str());
}

void Recording::maintenance(std::uint64_t before, bool retry, std::array<int, 2> places,
                            std::optional<std::array<int, 4>> resources_before,
                            std::optional<std::array<int, 4>> resources_after, std::optional<int> cooldown) {
    std::ostringstream fields;
    fields << ",\"places\":";
    integers(fields, places);
    if (resources_before && resources_after && cooldown) {
        fields << ",\"resources_before\":";
        integers(fields, *resources_before);
        fields << ",\"resources_after\":";
        integers(fields, *resources_after);
        fields << ",\"cooldown_ms\":" << *cooldown;
    }
    command(retry ? "maintenance-retry" : "maintenance", before, fields.str());
}

void Recording::damage(std::uint64_t before, bool repair, std::array<float, 3> position, float health_before,
                       std::optional<float> health_after, bool cargo_marked) {
    std::ostringstream fields;
    fields << ",\"target\":6,\"weapon\":" << int(repair ? WEAPON_REPAIR : WEAPON_LASER)
        << ",\"attacker_side\":2,\"reported_death\":" << (repair ? "false" : "true")
        << ",\"origin_bit_patterns\":";
    float_bits(fields, position);
    fields << ",\"direction_bit_patterns\":[0,0,1065353216],\"health_before_bits\":"
        << std::bit_cast<std::uint32_t>(health_before);
    if (health_after) fields << ",\"health_after_bits\":" << std::bit_cast<std::uint32_t>(*health_after);
    else fields << ",\"cargo_marked\":" << (cargo_marked ? "true" : "false");
    command(repair ? "repair" : "lethal-laser", before, fields.str());
}

void Recording::snapshot(const std::string &observation) {
    MG_CHECK(!observation.empty() && observation.front() == '{');
    output_ << "{\"type\":\"snapshot\"," << observation.substr(1) << '\n';
    for (unsigned char value : observation) {
        hash_ ^= value;
        hash_ *= 1099511628211ull;
    }
    hash_ ^= '\n';
    hash_ *= 1099511628211ull;
    ++observations_;
    MG_CHECK(output_.good());
}

void Recording::finish(std::span<const std::array<float, 3>> standing_poses, std::optional<float> separation) {
    output_.close();
    MG_CHECK(output_.good());
    std::ostringstream tail;
    tail << "{\"type\":\"complete\",\"frame\":" << session_.frame() << ",\"time_ms\":" << session_.time()
        << ",\"rng_draws\":" << random::draw_count() << ",\"observations\":" << observations_
        << ",\"observation_digest_low\":" << std::uint32_t(hash_)
        << ",\"observation_digest_high\":" << std::uint32_t(hash_ >> 32);
    if (!standing_poses.empty()) {
        MG_CHECK(separation);
        tail << ",\"standing_pose_bit_patterns\":[";
        for (std::size_t i = 0; i < standing_poses.size(); ++i) {
            if (i) tail << ',';
            float_bits(tail, standing_poses[i]);
        }
        tail << "],\"separation_bits\":" << std::bit_cast<std::uint32_t>(*separation);
    }
    tail << "}\n";
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
} // namespace tests::carrier
