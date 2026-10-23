// MatrixGame - licensed under GPLv2 or any later version.
#include "benchmark_support.hpp"
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "Logic/MatrixEnvironment.h"
#include "Interface/CConstructor.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <float.h>
#include <iostream>
#include <limits>

namespace {
using tests::battle::Profile;
using tests::battle::Session;
struct Options { random::Mode mode; int step; };

// The timed path hashes fixed-size typed observations without serialization or
// allocation. Inspection also retains the words for exact fresh-session checks.
struct Observation {
    std::uint64_t hash = 14695981039346656037ull;
    std::vector<std::int64_t> words;
    bool retain;
    explicit Observation(bool keep, size_t capacity) : retain(keep) { if (keep) words.reserve(capacity); }
    void add(std::int64_t value) {
        hash = (hash ^ static_cast<std::uint64_t>(value)) * 1099511628211ull;
        if (retain) words.push_back(value);
    }
};
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
void count(bench::Counters &out, std::string_view name, std::uint64_t value) {
    MG_CHECK(value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    out.add(name, static_cast<std::int64_t>(value));
}
void common(bench::Counters &out, const Session &session, std::uint64_t initial, const Observation &observation) {
    count(out, "completed_map_ticks", session.frame());
    count(out, "elapsed_simulation_ms", session.time());
    count(out, "rng_draws", random::draw_count() - initial);
    count(out, "typed_state_hash_low", static_cast<std::uint32_t>(observation.hash));
    count(out, "typed_state_hash_high", observation.hash >> 32);
}
CMatrixRobotAI *robot(int side) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == side) return object->AsRobot();
    throw std::runtime_error("Missing battle benchmark robot");
}

class LaserScene {
public:
    explicit LaserScene(Options options, bool retain)
        : options_(options), session_(Profile{options.mode, 82, options.step, 1000, false, true}),
          observation(retain, 100 * 24) {
        // Match the established robot_run(false) active prefix, without its trace
        // writer or later shooter-removal/idle continuation.
        assets_.install();
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 600;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {2, 0, 10};
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
        g_Config.m_Overheat[WEAPON_LASER_HEAT + 2] = 10;
        g_Config.m_Overheat[WEAPON_LASER_HEAT + 1] = 100;
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = .04f;
        shooter_ = robot(PLAYER_SIDE);
        target_ = robot(2);
        MG_CHECK(session_.matches(1, shooter_) && session_.matches(3, target_));
        tests::arm_battle_robot(*shooter_, RUK_WEAPON_LASER);
        target_->InitMaxHitpoint(50);
        MG_CHECK(session_.target_command(3));
        initial_draws_ = random::draw_count();
    }
    void run(bench::Counters &work) {
        float health = target_->GetHitPoint();
        MG_CHECK(bits(health) == bits(5.0f));
        for (int frame = 0; frame < 1000 / options_.step; ++frame) {
            session_.advance();
            MG_CHECK(session_.matches(1, shooter_) && session_.matches(3, target_));
            MG_CHECK(shooter_->IsLiveRobot() && target_->IsLiveRobot());
            MG_CHECK(shooter_->GetWeapon(0).m_Heat == 0 || shooter_->GetWeapon(0).m_Heat == 10);
            if (target_->GetHitPoint() != health) {
                health = target_->GetHitPoint();
                MG_CHECK(hits_ < impacts_.size());
                impacts_[hits_++] = session_.time();
                MG_CHECK(bits(health) == bits(float(50 - int(hits_) * 2) / 10.0f));
            }
            const auto draws = random::draw_count();
            observation.add(session_.frame());
            observation.add(session_.time());
            observation.add(draws);
            for (unsigned id = 1; id <= 4; ++id) {
                const auto state = session_.robot(id);
                MG_CHECK(state.alive && std::isfinite(state.x) && std::isfinite(state.y));
                observation.add(bits(state.x)); observation.add(bits(state.y));
                observation.add(state.order); observation.add(state.target);
            }
            observation.add(bits(health));
            observation.add(shooter_->GetWeapon(0).m_Heat);
            observation.add(shooter_->GetEnv()->m_LastFire);
            observation.add(shooter_->GetEnv()->m_LastHitTarget);
            MG_CHECK(random::draw_count() == draws);
        }
        constexpr std::array<int, 9> ten{200, 300, 400, 500, 600, 700, 800, 900, 1000};
        constexpr std::array<int, 8> sixteen{224, 320, 432, 528, 624, 720, 832, 928};
        MG_CHECK(options_.step == 10 ? hits_ == ten.size() && impacts_ == ten :
            hits_ == sixteen.size() && std::equal(sixteen.begin(), sixteen.end(), impacts_.begin()));
        MG_CHECK(shooter_->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(shooter_->GetEnv()->m_LastFire > 0 && shooter_->GetEnv()->m_LastHitTarget > 0);
        MG_CHECK(shooter_->GetEnv()->m_TargetAttack == target_);
        session_.check_running();
        common(work, session_, initial_draws_, observation);
        count(work, "health_decrease_events", hits_);
        count(work, "internal_damage_units", 50 - Float2Int(health * 10));
        count(work, "terminal_health_bits", bits(health));
        constexpr const char *names[]{"impact_1_ms", "impact_2_ms", "impact_3_ms", "impact_4_ms", "impact_5_ms",
            "impact_6_ms", "impact_7_ms", "impact_8_ms", "impact_9_ms"};
        for (size_t i = 0; i < impacts_.size(); ++i) count(work, names[i], impacts_[i]);
        count(work, "last_fire_ms", shooter_->GetEnv()->m_LastFire);
        count(work, "last_hit_ms", shooter_->GetEnv()->m_LastHitTarget);
    }
    std::string metadata() const { return session_.metadata("active-laser-benchmark-v1"); }
private:
    Options options_;
    tests::BattleCombatAssets assets_; // Outlives Session's effect/catalog users.
    Session session_;
    CMatrixRobotAI *shooter_ = nullptr, *target_ = nullptr;
    std::uint64_t initial_draws_ = 0;
    std::array<int, 9> impacts_{};
    size_t hits_ = 0;
public:
    Observation observation;
};

// The exact no-loss strategic-capture fixture's definitions, including the
// resource/price bounds that prevent production from growing the population.
struct Definitions {
    Definitions() {
        MG_CHECK(!SSpecialBot::m_AIRobotTypeList && SSpecialBot::m_AIRobotTypeCnt == 0);
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        std::fill(std::begin(g_Config.m_WeaponStrengthAI), std::end(g_Config.m_WeaponStrengthAI), 0);
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_MACHINEGUN] = 50;
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_MACHINEGUN - 1) * MAX_RESOURCES + resource] = 60;
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_REPAIR - 1) * MAX_RESOURCES + resource] = 60;
            for (auto &cannon : g_Config.m_CannonsProps) cannon.m_Resources[resource] = 1000;
        }
        for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
            g_Config.m_Timings[resource] = 100000;
        std::array<SRobotWeaponMatrix, ROBOT_ARMOR_CNT> capacities{};
        capacities[RUK_ARMOR_MONOSTACK - 1].common = 3;
        CBlockPar input;
        input.ParAdd(L"1", L"P,1,GGR"); input.ParAdd(L"1", L"P,1,GR");
        SSpecialBot::LoadAIRobotType(input, capacities);
        MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
        for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Resources[resource] == 180);
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Resources[resource] == 120);
        }
    }
    ~Definitions() { SSpecialBot::ClearAIRobotType(); }
};
CMatrixBuilding *building(int owner, EBuildingType kind, float x, float y) {
    auto owned = tests::make_battle_building(*tests::load_battle_mesh({}), owner, kind, x, y);
    auto *result = owned.get();
    result->m_TrueColor.m_Color = owner ? 0xff000000 | g_MatrixMap->GetSideColor(owner) : 0;
    result->m_TrueColor.m_ColoredCnt = owner ? MAX_ZAHVAT_POINTS : 0;
    g_MatrixMap->AddObject(owned.release(), true);
    return result;
}
struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using Core = std::unique_ptr<SObjectCore, ReleaseCore>;
Profile strategy_profile(Options options) {
    Profile result{options.mode, 84, options.step, 48000 / options.step, false, false};
    result.layout = tests::NavigationLayout::MultiRegion;
    return result;
}

class StrategyScene {
public:
    explicit StrategyScene(Options options, bool retain)
        : options_(options), session_(strategy_profile(options)), observation(retain, (48000 / options.step) * 120) {
        auto &map = *g_MatrixMap;
        auto &side = *map.GetSideById(2);
        map.GetPlayerSide()->m_Color = 0x0000ff00; side.m_Color = 0x00ff0000;
        for (int resource = 0; resource < MAX_RESOURCES; ++resource)
            side.SetResourceAmount(static_cast<ERes>(resource), 0);
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsRobot()) continue;
            auto *unit = object->AsRobot();
            if (unit->GetSide() == PLAYER_SIDE) { unit->SetMaxSpeed(0); unit->m_GroupSpeed = 0; }
        }
        home_ = building(2, BUILDING_BASE, 500, 100);
        primary_ = building(0, BUILDING_TITAN, 100, 500);
        backup_ = building(0, BUILDING_ENERGY, 200, 100);
        primary_core_.reset(primary_->GetCore(DEBUG_CALL_INFO));
        home_core_.reset(home_->GetCore(DEBUG_CALL_INFO));
        backup_core_.reset(backup_->GetCore(DEBUG_CALL_INFO));
        int home_cells = 0;
        for (int y = 0; y < map.m_Size.y; ++y) for (int x = 0; x < map.m_Size.x; ++x)
            home_cells += map.UnitGet(x, y)->m_Base == home_;
        MG_CHECK(home_cells == 9 && side.m_TeamCnt == 3);
        size_t count = 0;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsRobot()) continue;
            MG_CHECK(count < robots_.size());
            robots_[count++] = object->AsRobot();
        }
        MG_CHECK(count == robots_.size());
        for (auto *unit : robots_) {
            MG_CHECK(std::abs(unit->GetGeoCenter().x - unit->m_PosX) < .001f);
            MG_CHECK(std::abs(unit->GetGeoCenter().y - unit->m_PosY) < .001f);
            unit->DelLT(); unit->AddLT(); // The existing loaded-map scheduler ordering.
        }
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == home_);
        initial_draws_ = random::draw_count();
    }
    void run(bench::Counters &work) {
        auto &map = *g_MatrixMap;
        auto &side = *map.GetSideById(2);
        while (session_.frame() < 48000 / options_.step) {
            if (session_.time() == 6000) {
                MG_CHECK(primary_order_ > 0 && primary_order_ <= capture_action_);
                MG_CHECK(forward_frames_ > 0 && capture_frames_ > 0 && crossed_ > 0);
                MG_CHECK(primary_->GetSide() == 0 && primary_->m_TrueColor.m_ColoredCnt == 0);
                MG_CHECK(std::any_of(robots_.begin(), robots_.end(), [&](auto *unit) {
                    return unit->GetSide() == 2 && unit->GetCaptureFactory() == primary_;
                }));
                if (options_.mode == random::Mode::LegacyCRT && options_.step == 10)
                    MG_CHECK(primary_order_ < capture_action_);
            }
            session_.advance();
            MG_CHECK(!map.IsPaused());
            MG_CHECK(home_core_->m_Object == home_ && primary_core_->m_Object == primary_ &&
                backup_core_->m_Object == backup_);
            const auto draws = random::draw_count();
            observation.add(session_.time()); observation.add(draws);
            int live_robots = 0;
            for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
                MG_CHECK(!object->IsCannon());
                if (object->IsRobot()) ++live_robots;
            }
            MG_CHECK(live_robots == 4 && home_->m_BS.GetItemsCnt() == 0);
            for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                MG_CHECK(side.GetResourcesAmount(static_cast<ERes>(resource)) == 0);
            MG_CHECK(side.m_TeamCnt == 3);
            for (int t = 0; t < side.m_TeamCnt; ++t) {
                const auto &team = side.m_Team[t];
                MG_CHECK(team.m_Action.m_RegionPathCnt >= 0 && team.m_Action.m_RegionPathCnt <= REGION_PATH_MAX_CNT);
                observation.add(team.m_Action.m_Type); observation.add(team.m_Action.m_Region);
                observation.add(team.m_RegionMass); observation.add(team.m_RegionNerestBase);
                observation.add(team.m_RobotCnt); observation.add(team.m_GroupCnt);
                observation.add(team.m_Action.m_RegionPathCnt);
                for (int p = 0; p < team.m_Action.m_RegionPathCnt; ++p) observation.add(team.m_Action.m_RegionPath[p]);
                if (!team.m_RobotCnt) continue;
                if (team.m_Action.m_Type == mlat_Forward) {
                    ++forward_frames_; MG_CHECK(team.m_Action.m_Region == 0);
                }
                if (team.m_Action.m_Type == mlat_Capture) {
                    ++capture_frames_;
                    if (capture_action_ < 0) capture_action_ = session_.time();
                }
                if (team.m_Action.m_Type == mlat_Defence) {
                    ++defence_frames_;
                    late_defence_ |= session_.time() > 40000;
                }
            }
            if (session_.frame() == 1) {
                const auto &team = side.m_Team[0];
                MG_CHECK(team.m_Action.m_Type == mlat_Forward && team.m_RegionMass == 1);
                MG_CHECK(team.m_Action.m_RegionPathCnt == 2 && team.m_Action.m_RegionPath[0] == 1 &&
                    team.m_Action.m_RegionPath[1] == 0 && team.m_RoadPath && team.m_RoadPath->m_ListCnt > 0);
            }
            for (size_t i = 0; i < robots_.size(); ++i) {
                auto *unit = robots_[i];
                MG_CHECK(session_.matches(static_cast<unsigned>(i + 1), unit) && unit->IsLiveRobot());
                MG_CHECK(std::isfinite(unit->m_PosX) && std::isfinite(unit->m_PosY));
                MG_CHECK(unit->m_PosX >= 0 && unit->m_PosX < 640 && unit->m_PosY >= 0 && unit->m_PosY < 640);
                auto *target = unit->GetCaptureFactory();
                const int target_id = !target ? 0 : target == primary_ ? 1 : target == backup_ ? 2 : target == home_ ? 3 : -1;
                MG_CHECK(target_id >= 0 && (session_.time() < 45000 || !target));
                const int group = unit->GetGroupLogic();
                observation.add(unit->GetTeam()); observation.add(group); observation.add(unit->GetRegion());
                observation.add(target_id); observation.add(bits(unit->m_PosX)); observation.add(bits(unit->m_PosY));
                if (unit->GetSide() != 2) continue;
                MG_CHECK(unit->GetTeam() >= 0 && unit->GetTeam() < side.m_TeamCnt);
                MG_CHECK(group >= -1 && group < MAX_LOGIC_GROUP);
                if (group < 0) {
                    if (!ungrouped_since_[i]) ungrouped_since_[i] = session_.time();
                    MG_CHECK(session_.time() - ungrouped_since_[i] < 200);
                } else {
                    ungrouped_since_[i] = 0;
                    const auto &logic = side.m_LogicGroup[group];
                    observation.add(logic.RobotsCnt()); observation.add(logic.m_Team);
                    observation.add(logic.m_Action.m_Type); observation.add(logic.m_Action.m_Region);
                }
                if (unit->GetRegion() == 0) { ++crossed_; if (crossing_time_ < 0) crossing_time_ = session_.time(); }
                if (target == primary_ && primary_order_ < 0) primary_order_ = session_.time();
                if (target == backup_ && backup_order_ < 0) backup_order_ = session_.time();
            }
            for (auto *factory : {primary_, backup_, home_}) {
                observation.add(factory->GetSide()); observation.add(factory->m_TrueColor.m_ColoredCnt);
                observation.add(factory->m_TrueColor.m_Color);
            }
            if (primary_->m_TrueColor.m_ColoredCnt > 0 && primary_paint_ < 0) primary_paint_ = session_.time();
            if (primary_->GetSide() == 2 && primary_owned_ < 0) primary_owned_ = session_.time();
            if (backup_->GetSide() == 2 && backup_owned_ < 0) backup_owned_ = session_.time();
            if (session_.time() >= 45000) for (auto *factory : {primary_, backup_}) {
                MG_CHECK(factory->GetSide() == 2 && factory->m_TrueColor.m_ColoredCnt == MAX_ZAHVAT_POINTS);
                MG_CHECK(factory->m_TrueColor.m_Color == (0xff000000 | side.m_Color));
            }
            MG_CHECK(random::draw_count() == draws);
        }
        MG_CHECK(forward_frames_ > 0 && capture_frames_ > 0 && crossed_ > 0 && late_defence_);
        MG_CHECK(primary_paint_ > 6000 && primary_owned_ > primary_paint_);
        MG_CHECK(backup_order_ > 0 && backup_owned_ > backup_order_);
        session_.check_running();
        common(work, session_, initial_draws_, observation);
        count(work, "occupied_team_forward_frames", forward_frames_);
        count(work, "occupied_team_capture_frames", capture_frames_);
        count(work, "occupied_team_defence_frames", defence_frames_);
        count(work, "computer_robot_region0_frames", crossed_);
        count(work, "first_region_crossing_ms", crossing_time_);
        count(work, "first_capture_action_ms", capture_action_);
        count(work, "primary_order_ms", primary_order_);
        count(work, "primary_paint_ms", primary_paint_);
        count(work, "primary_owned_ms", primary_owned_);
        count(work, "backup_order_ms", backup_order_);
        count(work, "backup_owned_ms", backup_owned_);
        count(work, "terminal_owned_factories", (primary_->GetSide() == 2) + (backup_->GetSide() == 2));
        count(work, "terminal_primary_paint_points", primary_->m_TrueColor.m_ColoredCnt);
        count(work, "terminal_backup_paint_points", backup_->m_TrueColor.m_ColoredCnt);
    }
    std::string metadata() const { return session_.metadata("active-strategy-benchmark-v1"); }
private:
    Options options_;
    Session session_;
    Definitions definitions_;
    CMatrixBuilding *home_ = nullptr, *primary_ = nullptr, *backup_ = nullptr;
    Core primary_core_, home_core_, backup_core_;
    std::array<CMatrixRobotAI *, 4> robots_{};
    std::array<int, 4> ungrouped_since_{};
    std::uint64_t initial_draws_ = 0;
    int forward_frames_ = 0, capture_frames_ = 0, defence_frames_ = 0, crossed_ = 0;
    int crossing_time_ = -1, capture_action_ = -1, primary_order_ = -1, backup_order_ = -1;
    int primary_paint_ = -1, primary_owned_ = -1, backup_owned_ = -1;
    bool late_defence_ = false;
public:
    Observation observation;
};

template<class Scene> class BattleSession final : public bench::Session {
public:
    explicit BattleSession(Options options) : options_(options) {}
    void prepare() override { scene_.reset(); scene_ = std::make_unique<Scene>(options_, false); }
    void run(bench::Counters &work) override { scene_->run(work); }
    void inspect(bench::Counters &details) override {
        scene_.reset();
        auto unrecorded = std::make_unique<Scene>(options_, false);
        bench::Counters without_retention;
        unrecorded->run(without_retention);
        unrecorded.reset();
        auto first = std::make_unique<Scene>(options_, true);
        first->run(details);
        MG_CHECK(details == without_retention);
        std::cerr << "Battle benchmark profile: " << first->metadata() << '\n';
        auto words = std::move(first->observation.words);
        first.reset();
        auto second = std::make_unique<Scene>(options_, true);
        bench::Counters repeated;
        second->run(repeated);
        MG_CHECK(details == repeated && words == second->observation.words);
        count(details, "exact_verified_observation_words", words.size());
        count(details, "exact_verified_sessions", 2);
        count(details, "retention_off_matching_sessions", 1);
    }
private:
    Options options_;
    std::unique_ptr<Scene> scene_;
};

std::vector<bench::Workload> workloads() {
    std::vector<bench::Workload> result;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const Options options{mode, step};
        const std::string profile = std::string(mode == random::Mode::ParkMiller ? "park_miller" : "legacy") + std::to_string(step);
        const std::string rng = mode == random::Mode::ParkMiller ? "ParkMiller" : "LegacyCRT";
        const std::vector<bench::Parameter> common_parameters{{"rng", rng}, {"step_ms", std::to_string(step)},
            {"robots", "4"}, {"timed_scope", "complete production map ticks plus typed observations/assertions/hash"},
            {"untimed", "setup/commands/destruction/exact fresh-session verification/metadata/JSON I/O"},
            {"render", "CPU fixture; no native draw"}, {"audio", "unavailable"}};
        auto laser_parameters = common_parameters;
        laser_parameters.insert(laser_parameters.end(), {{"seed", "82"}, {"scene", "laser-active-prefix-v1"},
            {"layout", "central-obstacle"}, {"armed_robots", "1"}, {"target_initial_internal_health", "50"},
            {"frames", std::to_string(1000 / step)}, {"elapsed_ms", std::to_string((1000 / step) * step)}});
        result.push_back({"combat.laser_tick." + profile,
            "Established laser engagement active prefix; actual hit times/damage/fire required; observation overhead included",
            std::move(laser_parameters), 1, [options] { return std::make_unique<BattleSession<LaserScene>>(options); }});
        auto strategy_parameters = common_parameters;
        strategy_parameters.insert(strategy_parameters.end(), {{"seed", "84"}, {"scene", "strategic-no-loss-v1"},
            {"layout", "multi-region"}, {"computer_robots", "2"}, {"computer_teams", "3"}, {"buildings", "3"},
            {"elapsed_ms", "48000"}, {"income_interval_ms", "100000"}, {"initial_computer_resources", "0"}});
        result.push_back({"strategy.capture_tick." + profile,
            "Established no-loss expansion/capture/defence journey; positive physical progression required; observation overhead included",
            std::move(strategy_parameters), 1, [options] { return std::make_unique<BattleSession<StrategyScene>>(options); }});
    }
    return result;
}
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Base::CMain::BaseInit();
#if defined(__MINGW32__)
    _controlfp(_PC_24, _MCW_PC);
#endif
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
#if defined(_MSC_VER) && defined(_DEBUG)
    tests::configure_crt_reports();
#endif
    const auto all = workloads();
    const int result = bench::run(argc, argv, all);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
