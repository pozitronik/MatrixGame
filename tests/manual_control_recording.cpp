// MatrixGame - licensed under GPLv2 or any later version.
#include "manual_control_recording.hpp"
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "input.hpp"
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace tests::manual {
namespace {
constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
std::vector<std::pair<std::filesystem::path, std::string>> completions;

void fold(std::uint64_t &hash, const std::string &value) {
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    hash *= fnv_prime; // A zero-byte separator between complete observations.
}
const char *chassis_name(int chassis) {
    MG_CHECK(chassis == RUK_CHASSIS_TRACK || chassis == RUK_CHASSIS_HOVERCRAFT);
    return chassis == RUK_CHASSIS_TRACK ? "track" : "hovercraft";
}
const char *continuation_name(int continuation) {
    MG_CHECK(continuation >= 0 && continuation <= 2);
    return continuation == 0 ? "original" : continuation == 1 ? "removed" : "live";
}
std::filesystem::path path_for(int chassis, random::Mode mode, int step, bool moving, int continuation, int replay) {
    return std::filesystem::current_path() / "scenario-traces" /
        (std::string("manual-control-") + chassis_name(chassis) + "-" + (moving ? "moving" : "stationary") + "-" +
         (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" + std::to_string(step) + "-" +
         continuation_name(continuation) + "-" + std::to_string(replay) + ".jsonl");
}
void invalidate(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    MG_CHECK(file.good());
    file.close();
    MG_CHECK(file.good());
}
}

struct View::Impl {
    battle::Session &session;
    CMatrixRobotAI *actor;
    std::size_t uid;
    bool aim_defined = false;
    Impl(battle::Session &s, CMatrixRobotAI &r) : session(s), actor(&r), uid(r.UID) {}
};
View::View(battle::Session &session, CMatrixRobotAI &actor) : impl_(std::make_unique<Impl>(session, actor)) {}
View::~View() = default;
void View::aim_assigned() { impl_->aim_defined = true; }

std::string View::snapshot(const char *phase) const {
    const auto draws = random::draw_count();
    const auto &s = *impl_;
    std::array<CMatrixRobotAI *, 6> actors{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        auto *robot = object->AsRobot();
        for (unsigned id = 1; id <= 4; ++id)
            if (s.session.matches(id, robot)) actors[id] = robot;
        if (robot == s.actor && robot->UID == s.uid) actors[5] = robot;
    }
    const auto identity = [&](const CMatrixMapStatic *object) {
        if (!object) return 0;
        for (unsigned id = 1; id < actors.size(); ++id)
            if (actors[id] == object) return int(id);
        return -1;
    };
    auto &side = *g_MatrixMap->GetPlayerSide();
    auto *robot = actors[5];
    std::ostringstream out;
    out << std::boolalpha << "{\"type\":\"snapshot\",\"state\":" << s.session.snapshot()
        << ",\"phase\":\"" << phase << "\",\"manual\":{\"id\":5,\"present\":" << (robot != nullptr);
    if (robot) {
        const bool alive = robot->IsLiveRobot();
        out << ",\"alive\":" << alive << ",\"state\":" << int(robot->m_CurrState);
        if (alive) {
            const auto group = robot->GetGroupLogic();
            out << ",\"chassis\":" << int(robot->m_Unit[0].u1.s1.m_Kind)
                << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosX)
                << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(robot->m_PosY)
                << ",\"health_bits\":" << std::bit_cast<std::uint32_t>(robot->GetHitPoint())
                << ",\"speed_bits\":" << std::bit_cast<std::uint32_t>(robot->GetMaxSpeed())
                << ",\"forward_x_bits\":" << std::bit_cast<std::uint32_t>(robot->m_Forward.x)
                << ",\"forward_y_bits\":" << std::bit_cast<std::uint32_t>(robot->m_Forward.y)
                << ",\"group\":" << group << ",\"in_place\":" << robot->PLIsInPlace();
            CPoint move(-1, -1);
            const bool moving = robot->GetMoveToCoords(move);
            out << ",\"moving\":" << moving << ",\"move\":[" << move.x << ',' << move.y << ']';
            auto &env = *robot->GetEnv();
            out << ",\"place\":" << env.m_Place << ",\"place_add\":[" << env.m_PlaceAdd.x << ',' << env.m_PlaceAdd.y
                << "],\"enemy_count\":" << env.GetEnemyCnt() << ",\"target\":" << identity(env.m_Target)
                << ",\"attack_target\":" << identity(env.m_TargetAttack) << ",\"group_state\":{\"present\":"
                << (group >= 0);
            if (group >= 0) {
                MG_CHECK(group < MAX_LOGIC_GROUP);
                const auto &order = side.m_PlayerGroup[group];
                out << ",\"order\":" << int(order.Order()) << ",\"target\":" << identity(order.m_Obj)
                    << ",\"destination\":[" << order.m_To.x << ',' << order.m_To.y << "],\"war\":" << order.IsWar();
            }
            out << "},\"queue\":[";
            for (int i = 0; i < robot->GetOrdersInPool(); ++i) {
                if (i) out << ',';
                const auto &order = *robot->GetOrder(i);
                out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                    << identity(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
            }
            const auto &weapon = robot->GetWeapon(0);
            out << "],\"weapon\":{\"present\":" << weapon.IsEffectPresent();
            if (weapon.IsEffectPresent())
                out << ",\"type\":" << int(weapon.GetWeaponType())
                    << ",\"range_bits\":" << std::bit_cast<std::uint32_t>(weapon.GetWeaponDist())
                    << ",\"heat\":" << weapon.m_Heat << ",\"heat_period\":" << weapon.m_HeatPeriod
                    << ",\"heat_mod\":" << weapon.m_HeatMod << ",\"cooldown_period\":" << weapon.m_CoolDownPeriod
                    << ",\"cooldown_mod\":" << weapon.m_CoolDownMod << ",\"on\":" << weapon.m_On;
            out << '}';
        }
    }
    out << "},\"control\":{\"id\":" << identity(side.GetArcadedObject())
        << ",\"arcade\":" << side.IsArcadeMode() << ",\"robot_mode\":" << side.IsRobotMode()
        << "},\"input\":{\"forward\":" << Input::isVKeyPressed(g_Config.m_KeyActions[KA_UNIT_FORWARD])
        << ",\"right\":" << Input::isVKeyPressed(g_Config.m_KeyActions[KA_UNIT_RIGHT])
        << ",\"enter\":" << Input::isVKeyPressed(VK_RETURN)
        << ",\"mouse_left\":" << Input::isVKeyPressed(VK_LBUTTON) << "},\"aim\":{\"defined\":"
        << s.aim_defined << ",\"kind\":\"";
    auto *trace = g_MatrixMap->m_TraceStopObj;
    // Engine sentinels are classified by pointer equality; no trace pointer is dereferenced here.
    const char *kind = trace == TRACE_STOP_NONE ? "none" : trace == TRACE_STOP_LANDSCAPE ? "landscape" :
        trace == TRACE_STOP_WATER ? "water" : "object";
    out << kind << "\",\"object\":" << (std::string_view(kind) == "object" ? identity(trace) : 0);
    if (s.aim_defined) {
        const auto &point = g_MatrixMap->m_TraceStopPos;
        out << ",\"x_bits\":" << std::bit_cast<std::uint32_t>(point.x)
            << ",\"y_bits\":" << std::bit_cast<std::uint32_t>(point.y)
            << ",\"z_bits\":" << std::bit_cast<std::uint32_t>(point.z);
    }
    out << "},\"reservations\":[";
    for (unsigned id : {3u, 4u}) {
        if (id == 4) out << ',';
        auto *reserved = actors[id];
        out << "{\"id\":" << id << ",\"present\":" << (reserved != nullptr);
        if (reserved) {
            const bool alive = reserved->IsLiveRobot();
            out << ",\"alive\":" << alive;
            if (alive) out << ",\"place\":" << reserved->GetEnv()->m_Place;
        }
        out << '}';
    }
    out << "]}";
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}

void invalidate_original(int chassis) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) for (bool moving : {false, true})
            invalidate(path_for(chassis, mode, step, moving, 0, 0));
}
void invalidate_enemy(int chassis, random::Mode mode, int step, bool moving) {
    for (int continuation : {1, 2}) for (int replay : {0, 1})
        invalidate(path_for(chassis, mode, step, moving, continuation, replay));
}

struct Recording::Impl {
    battle::Session &session;
    View &view;
    std::filesystem::path path;
    std::ofstream file;
    std::uint64_t observation_hash = fnv_offset;
    std::size_t observations = 0;
    Impl(battle::Session &s, View &v, std::filesystem::path p)
        : session(s), view(v), path(std::move(p)), file(path, std::ios::trunc) { MG_CHECK(file.good()); }
    void write(const std::string &record) {
        file << record << '\n';
        file.flush();
        MG_CHECK(file.good());
    }
};
Recording::Recording(battle::Session &session, View &view, int chassis, random::Mode mode, int step,
                     bool moving, int continuation, int replay)
    : impl_(std::make_unique<Impl>(session, view, path_for(chassis, mode, step, moving, continuation, replay))) {
    auto header = session.metadata(continuation == 0 ? "manual-chassis-lifecycle-v1" :
                                   "manual-live-enemy-continuation-v1");
    header.pop_back();
    header += ",\"manual_observation\":\"direct-control-v1\",\"chassis\":\"" + std::string(chassis_name(chassis)) +
        "\",\"chassis_kind\":" + std::to_string(chassis) + ",\"moving_target\":" + (moving ? "true" : "false") +
        ",\"continuation\":\"" + continuation_name(continuation) + "\",\"manual_actor\":5,\"forward_key\":" +
        std::to_string(g_Config.m_KeyActions[KA_UNIT_FORWARD]) + ",\"right_key\":" +
        std::to_string(g_Config.m_KeyActions[KA_UNIT_RIGHT]) + ",\"enter_key\":13,\"mouse_button\":1,\"trace\":" +
#ifdef _TRACE
        "true}";
#else
        "false}";
#endif
    impl_->write(header);
    impl_->write(view.snapshot("initial"));
}
Recording::~Recording() = default;
void Recording::state(const char *phase) {
    const auto observation = impl_->view.snapshot(phase);
    fold(impl_->observation_hash, observation);
    ++impl_->observations;
    impl_->write(observation);
}
void Recording::command(const char *name, const char *kind, std::uint64_t draws_before,
                        int code, int value, int x, int y) {
    auto &s = *impl_;
    const auto draws_after = random::draw_count();
    std::ostringstream out;
    out << "{\"type\":\"command\",\"frame\":" << s.session.frame() << ",\"time_ms\":" << s.session.time()
        << ",\"name\":\"" << name << "\",\"kind\":\"" << kind << "\",\"code\":" << code << ",\"value\":" << value
        << ",\"point\":[" << x << ',' << y << "],\"rng_draws_before\":" << draws_before
        << ",\"rng_draws_after\":" << draws_after << '}';
    s.write(out.str());
    s.write(s.view.snapshot(name));
    MG_CHECK(random::draw_count() == draws_after);
}
void Recording::finish(const std::vector<std::string> &existing_frames, std::size_t prefix_frames,
                       int exit_time, int retry_time) {
    auto &s = *impl_;
    s.write(s.view.snapshot("finished"));
    std::uint64_t existing_hash = fnv_offset;
    for (const auto &frame : existing_frames) fold(existing_hash, frame);
    std::ostringstream out;
    out << "{\"type\":\"complete\",\"frame\":" << s.session.frame() << ",\"time_ms\":" << s.session.time()
        << ",\"manual_result\":{\"exit_ms\":" << exit_time << ",\"retry_ms\":" << retry_time
        << ",\"existing_frames\":" << existing_frames.size() << ",\"existing_prefix_frames\":" << prefix_frames
        << ",\"existing_digest_low\":" << std::uint32_t(existing_hash)
        << ",\"existing_digest_high\":" << std::uint32_t(existing_hash >> 32)
        << ",\"observed_frames\":" << s.observations << ",\"observation_digest_low\":"
        << std::uint32_t(s.observation_hash) << ",\"observation_digest_high\":"
        << std::uint32_t(s.observation_hash >> 32) << ",\"terminal_rng_draws\":" << random::draw_count() << "}}";
    completions.emplace_back(s.path, out.str());
}
void complete_after_cleanup() {
    for (const auto &[path, completion] : completions) {
        std::ofstream file(path, std::ios::app);
        MG_CHECK(file.good());
        file << completion << '\n';
        file.flush();
        MG_CHECK(file.good());
    }
}

} // namespace tests::manual
