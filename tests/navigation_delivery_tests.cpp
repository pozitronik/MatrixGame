// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_flyer.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "test_support.hpp"
#include "carrier_recording.hpp"
#include <cmath>
#include <memory>
#include <array>
#include <set>
#include <vector>

namespace tests::navigation {
namespace {
using battle::Session;
struct CraterAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct CraterCatalog {
    SSpotProperties previous = CraterAccess::m_SpotProperties[SPOT_VORONKA];
    ~CraterCatalog() { CraterAccess::m_SpotProperties[SPOT_VORONKA] = previous; }
    void install() {
        auto &spot = CraterAccess::m_SpotProperties[SPOT_VORONKA];
        spot = {};
        spot.color = 0xffffffff;
        spot.func = SpotTaktConstant;
        spot.texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-combat"));
        spot.ttl = 30000;
    }
};
struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, ReleaseCore>;
battle::Profile profile(random::Mode mode, int step) {
    battle::Profile p;
    p.mode = mode;
    p.seed = 81;
    p.step_ms = step;
    p.max_frames = 6000;
    p.audio = false;
    p.initially_selected = false;
    p.layout = NavigationLayout::Open;
    p.unavailable_icon_targets = true;
    return p;
}
struct Delivery {
    CMatrixFlyer *carrier;
    CMatrixRobotAI *cargo;
    CoreLease carrier_core;
    CoreLease cargo_core;
    explicit Delivery(float angle, tests::carrier::Recording *recording = nullptr) {
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0);
        // Place 3 is a declared free place in the open navigation map.
        MG_CHECK(g_MatrixMap->m_RN.m_PlaceCnt > 3);
        const CPoint destination = g_MatrixMap->m_RN.m_Place[3].m_Pos;
        const D3DXVECTOR2 landing(float(destination.x * GLOBAL_SCALE_MOVE),
                                 float(destination.y * GLOBAL_SCALE_MOVE));
        const auto draws = random::draw_count();
        g_MatrixMap->GetPlayerSide()->OrderFlyer(landing, FO_GIVE_BOT, angle, 3, destination, 0);
        if (recording) recording->request(draws, angle, {landing.x, landing.y}, 3, {destination.x, destination.y});
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
        carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->CarryingRobot());
        cargo = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
        carrier_core.reset(carrier->GetCore(DEBUG_CALL_INFO));
        cargo_core.reset(cargo->GetCore(DEBUG_CALL_INFO));
        MG_CHECK(cargo->m_CurrState == ROBOT_CARRYING && cargo->GetEnv()->m_Place == 3);
        const int group = cargo->GetGroupLogic();
        MG_CHECK(group >= 0);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_Attack);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].m_To == destination);
    }
};
void finite(const D3DXVECTOR3 &p) {
    MG_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}
float finite(const CMatrixRobotAI &r) {
    MG_CHECK(std::isfinite(r.m_PosX) && std::isfinite(r.m_PosY));
    const float height = r.GetMatrix()._43;
    MG_CHECK(std::isfinite(height));
    return height;
}
}
void delivery_flights() {
    tests::carrier::invalidate("flights");
    // Recorded production lifecycle boundaries for seed 81 and the declared
    // geometries; rows are PM/10, PM/16, CRT/10 and CRT/16, then variants 0..3.
    constexpr int timing[4][4][3] = {
        {{3420, 3740, 6690}, {5610, 6280, 10650}, {4760, 5140, 8740}, {6110, 6810, 11610}},
        {{3424, 3744, 6704}, {5600, 6272, 10656}, {4768, 5152, 8752}, {6112, 6800, 11616}},
        {{5410, 5730, 10600}, {7760, 8420, 14860}, {6500, 6880, 12150}, {8120, 8820, 15560}},
        {{5408, 5728, 10608}, {7760, 8432, 14864}, {6496, 6880, 12160}, {8128, 8832, 15552}},
    };
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int step : {10, 16}) for (int variant = 0; variant < 4; ++variant) {
        BattleMaterialProfile materials;
        Session session(profile(mode, step));
        auto &order = install_battle_transport();
        // Pair approach direction with distinct declared flight geometry.
        order.ParSetAdd(L"Distance", variant % 2 ? L"450" : L"300");
        order.ParSetAdd(L"Height", variant >= 2 ? L"160" : L"100");
        order.ParSetAdd(L"LandHeight", variant % 2 ? L"70" : L"40");
        MG_CHECK(session.remove(2) && session.remove(4));
        tests::carrier::Recording recording(session, mode, step, "flights", variant);
        Delivery delivery(float(variant) * float(M_PI / 2), &recording);
        const D3DXVECTOR3 start = delivery.carrier->GetPos();
        int release = -1, landing = -1, retired = -1;
        float released_x = 0, released_y = 0;
        float previous_z = 0;
        int falling_frames = 0;
        const auto destination = g_MatrixMap->m_RN.m_Place[3].m_Pos;
        const auto sample = [&](std::optional<std::array<float, 3>> position = {},
                                std::optional<float> height = {}) {
            const tests::carrier::Pair pair{delivery.carrier, delivery.cargo, delivery.carrier_core.get(),
                delivery.cargo_core.get(), 3, {destination.x, destination.y}, position, height,
                tests::carrier::Progress{release, landing, retired, falling_frames}};
            const auto observation = tests::carrier::observe(session, std::span(&pair, 1));
            recording.snapshot(observation);
        };
        sample(std::array<float, 3>{start.x, start.y, start.z});
        while (session.time() < 30000) {
            session.advance();
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            const float height = finite(*delivery.cargo);
            std::optional<std::array<float, 3>> carrier_position;
            if (delivery.carrier_core->m_Object) {
                const auto &position = delivery.carrier->GetPos();
                finite(position);
                carrier_position = std::array<float, 3>{position.x, position.y, position.z};
            }
            else if (retired < 0) retired = session.time();
            if (release < 0 && delivery.cargo->m_CurrState != ROBOT_CARRYING) {
                release = session.time();
                MG_CHECK(delivery.cargo->m_CurrState == ROBOT_FALLING);
                MG_CHECK(delivery.carrier_core->m_Object && !delivery.carrier->CarryingRobot());
                released_x = delivery.cargo->m_PosX;
                released_y = delivery.cargo->m_PosY;
                previous_z = delivery.cargo->GetMatrix()._43;
                MG_CHECK(previous_z > g_MatrixMap->GetZ(released_x, released_y));
                MG_CHECK(delivery.carrier->GetPos() != start);
            }
            if (release >= 0 && delivery.cargo->m_CurrState == ROBOT_FALLING) {
                ++falling_frames;
                MG_CHECK(delivery.cargo->m_PosX == released_x && delivery.cargo->m_PosY == released_y);
                MG_CHECK(delivery.cargo->GetMatrix()._43 <= previous_z);
                previous_z = delivery.cargo->GetMatrix()._43;
            }
            if (landing < 0 && release >= 0 && delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD)
                landing = session.time();
            sample(carrier_position, height);
        }
        std::cout << session.metadata("synthetic-navigation-delivery-v1") << '\n';
        std::cout << "variant=" << variant << " release=" << release << " landing=" << landing
                  << " retirement=" << retired << " position=" << delivery.cargo->m_PosX << ','
                  << delivery.cargo->m_PosY << " in_place=" << delivery.cargo->PLIsInPlace() << '\n';
        MG_CHECK(release > 0 && landing > release && retired > landing && falling_frames > 1);
        const int row = (mode == random::Mode::LegacyCRT ? 2 : 0) + (step == 16 ? 1 : 0);
        MG_CHECK(release == timing[row][variant][0] && landing == timing[row][variant][1]);
        MG_CHECK(retired == timing[row][variant][2]);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && !delivery.carrier_core->m_Object);
        MG_CHECK(delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(delivery.cargo->PLIsInPlace());
        MG_CHECK(delivery.cargo->m_PosX == 500 && delivery.cargo->m_PosY == 500);
        MG_CHECK(session.rejected_icon_targets() == 1);
        recording.finish();
    }
}
void delivery_carrier_damage() {
    tests::carrier::invalidate("damage");
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int stage = 0; stage < 3; ++stage) {
        BattleMaterialProfile materials;
        BattleCombatAssets effects;
        CraterCatalog crater;
        Session session(profile(mode, 10));
        effects.install();
        crater.install();
        install_battle_transport();
        MG_CHECK(session.remove(2) && session.remove(4));
        tests::carrier::Recording recording(session, mode, 10, "damage", stage);
        Delivery delivery(0, &recording);
        const auto destination = g_MatrixMap->m_RN.m_Place[3].m_Pos;
        const auto sample = [&](std::optional<std::array<float, 3>> position = {}) {
            const tests::carrier::Pair pair{delivery.carrier, delivery.cargo, delivery.carrier_core.get(),
                delivery.cargo_core.get(), 3, {destination.x, destination.y}, position, {}, {}};
            const auto observation = tests::carrier::observe(session, std::span(&pair, 1));
            recording.snapshot(observation);
        };
        sample();
        for (;;) {
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            MG_CHECK(delivery.carrier_core->m_Object == delivery.carrier);
            const bool ready = stage == 0 ? session.time() >= 1000 :
                stage == 1 ? delivery.cargo->m_CurrState != ROBOT_CARRYING :
                             delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD;
            if (ready || session.time() >= 10000) break;
            session.advance();
            sample();
        }
        MG_CHECK(delivery.carrier_core->m_Object == delivery.carrier);
        MG_CHECK(stage == 0 ? delivery.cargo->m_CurrState == ROBOT_CARRYING :
                 stage == 1 ? delivery.cargo->m_CurrState == ROBOT_FALLING :
                              delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        const D3DXVECTOR3 position = delivery.carrier->GetPos();
        const float hp = delivery.carrier->GetHitPoint();
        MG_CHECK(hp == 1000);
        const auto draws = random::draw_count();
        MG_CHECK(!delivery.carrier->Damage(WEAPON_REPAIR, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
        const float repaired_hp = delivery.carrier->GetHitPoint();
        MG_CHECK(repaired_hp == hp && random::draw_count() == draws);
        recording.damage(draws, true, {position.x, position.y, position.z}, hp, repaired_hp, false);
        sample(std::array<float, 3>{position.x, position.y, position.z});
        g_Config.m_FlyerDamages[Weap2Index(WEAPON_LASER)] = {1000, 0, 0};
        const auto lethal_draws = random::draw_count();
        MG_CHECK(delivery.carrier->Damage(WEAPON_LASER, position, D3DXVECTOR3(0, 0, 1), 2, nullptr));
        MG_CHECK(!delivery.carrier_core->m_Object && g_MatrixMap->m_AD_Obj_cnt == 0);
        MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
        const bool cargo_marked = delivery.cargo->IsMustDie();
        MG_CHECK(cargo_marked == (stage == 0));
        recording.damage(lethal_draws, false, {position.x, position.y, position.z}, repaired_hp, {}, cargo_marked);
        sample();
        const int hit_time = session.time();
        while (session.time() < hit_time + 20000) {
            session.advance();
            sample();
        }
        std::cout << session.metadata("synthetic-navigation-carrier-damage-v1") << '\n';
        std::cout << "damage_stage=" << stage << " hit_time=" << hit_time
                  << " cargo_survived=" << bool(delivery.cargo_core->m_Object) << '\n';
        if (stage == 0) MG_CHECK(!delivery.cargo_core->m_Object);
        else {
            MG_CHECK(delivery.cargo_core->m_Object == delivery.cargo);
            MG_CHECK(delivery.cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && !delivery.cargo->IsMustDie());
            MG_CHECK(delivery.cargo->PLIsInPlace());
            MG_CHECK(delivery.cargo->m_PosX == 500 && delivery.cargo->m_PosY == 500);
        }
        recording.finish();
    }
}
void neighboring_delivery_landings() {
    tests::carrier::invalidate("neighbors");
    // Recorded landing ticks: PM/10, PM/16, CRT/10, CRT/16; then terrain
    // variants 0..2 and the two carriers in their request order.
    constexpr int landing_times[4][3][2] = {
        {{6520, 4170}, {6500, 4170}, {6470, 4090}},
        {{6528, 4176}, {6512, 4176}, {6480, 4096}},
        {{6150, 4670}, {6130, 4670}, {6160, 4700}},
        {{6160, 4672}, {6128, 4672}, {6160, 4704}},
    };
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int step : {10, 16}) for (int variant = 0; variant < 3; ++variant) {
        BattleMaterialProfile materials;
        Session session(profile(mode, step));
        install_battle_transport(60);
        auto &map = *g_MatrixMap;
        auto &side = *map.GetPlayerSide();
        auto &road = map.m_RN;
        const float slope = variant == 2 ? 0.25f : 0.0f;
        // Consistent height triangles and vertex normals, declared as initial
        // terrain data. Navigation restrictions are supplied, not regenerated.
        D3DXVECTOR3 normal(-slope, 0, 1);
        D3DXVec3Normalize(&normal, &normal);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            auto &unit = *map.UnitGet(x, y);
            unit.SetType(CELLFLAG_LAND);
            unit.a1 = unit.a2 = slope;
            unit.b1 = unit.b2 = 0;
            unit.c1 = unit.c2 = 200 + slope * x * GLOBAL_SCALE;
        }
        for (int y = 0; y <= 32; ++y) for (int x = 0; x <= 32; ++x) {
            auto &point = *map.PointGet(x, y);
            point.z = point.z_land = 200 + slope * x * GLOBAL_SCALE;
            point.n = normal;
        }
        MG_CHECK(road.m_PlaceCnt == 4);
        for (int i = 0; i < 4; ++i) road.m_Place[i].m_Pos = CPoint(24 + (i % 2) * 4, 24 + (i / 2) * 4);
        road.InitPL(BattleGeometry::movement_size, BattleGeometry::movement_size);
        for (int i = 0; i < 4; ++i)
            MG_CHECK(road.m_Place[i].m_Pos == CPoint(24 + (i % 2) * 4, 24 + (i / 2) * 4));
        if (variant != 0) {
            road.m_Place[1].m_Move = 31;
            // The rejected landing footprint occupies [28,32) by [24,28).
            for (int y = 0; y < BattleGeometry::movement_size; ++y)
              for (int x = 0; x < BattleGeometry::movement_size; ++x)
                for (int size = 1; size <= 5; ++size)
                    if (x < 32 && x + size > 28 && y < 28 && y + size > 24)
                        map.MoveGet(x, y)->m_Stop |= DWORD{31} << (6 * (size - 1));
        }
        MG_CHECK(session.remove(2) && session.remove(4));
        CMatrixRobotAI *donor = nullptr;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsRobot()) continue;
            auto *robot = object->AsRobot();
            robot->SetMaxSpeed(0);
            robot->m_GroupSpeed = 0;
            robot->GetEnv()->m_Place = -1;
            if (robot->GetSide() == 2) {
                // Keep the opposing sentinel outside both declared approach paths.
                robot->m_PosX = 540;
                robot->m_PosY = 100;
                robot->MapPosCalc();
                robot->JoinToGroup();
            }
            robot->RChange(MR_Matrix);
            robot->RNeed(MR_Matrix);
            if (robot->GetSide() == PLAYER_SIDE) donor = robot;
        }
        MG_CHECK(donor != nullptr);
        donor->GetEnv()->m_Place = 0; // Existing reservation; its owner stays outside the landing area.
        auto factory = make_battle_building(*donor->m_Unit[0].m_Graph->VO(), PLAYER_SIDE, BUILDING_TITAN, 100, 100);
        g_Config.m_MaintenanceTime = 1000;
        map.m_Difficulty.k_time_before_maintenance = 1;
        map.SetMaintenanceTime(0);
        auto resources = [&] {
            std::array<int, MAX_RESOURCES> result{};
            for (int i = 0; i < MAX_RESOURCES; ++i)
                result[i] = side.GetResourcesAmount(static_cast<ERes>(i));
            return result;
        };
        const auto before = resources();
        tests::carrier::Recording recording(session, mode, step, "neighbors", variant);
        const auto draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(map.m_AD_Obj_cnt == 2 && session.rejected_icon_targets() == 2);
        const int cooldown = map.BeforeMaintenanceTime();
        MG_CHECK(cooldown == 1000 && random::draw_count() == draws + 5);
        const auto resources_after = resources();
        MG_CHECK(resources_after == before);
        struct Cargo {
            CMatrixFlyer *carrier;
            CMatrixRobotAI *robot;
            CoreLease carrier_core;
            CoreLease robot_core;
            int place, group, expected_landing;
            CPoint destination;
            int release = -1, landing = -1, retired = -1, falling_samples = 0;
            D3DXVECTOR3 released{};
            float previous_z = 0;
            Cargo(CMatrixFlyer *flyer, int expected) : carrier(flyer), robot(static_cast<CMatrixRobotAI *>(flyer->GetCarryingRobot())),
                carrier_core(flyer->GetCore(DEBUG_CALL_INFO)), robot_core(robot->GetCore(DEBUG_CALL_INFO)),
                place(robot->GetEnv()->m_Place), group(robot->GetGroupLogic()), expected_landing(expected),
                destination(g_MatrixMap->m_RN.m_Place[place].m_Pos) {}
        };
        std::vector<Cargo> cargo;
        cargo.reserve(2);
        std::set<int> places;
        for (int i = 0; i < 2; ++i) {
            auto *carrier = static_cast<CMatrixFlyer *>(map.m_AD_Obj[i]);
            MG_CHECK(carrier->CarryingRobot());
            auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
            MG_CHECK(robot->GetEnv()->m_Place > 0 && robot->GetEnv()->m_Place < road.m_PlaceCnt);
            const int row = (mode == random::Mode::LegacyCRT ? 2 : 0) + (step == 16 ? 1 : 0);
            cargo.emplace_back(carrier, landing_times[row][variant][i]);
            const auto &item = cargo.back();
            MG_CHECK(places.insert(item.place).second);
            MG_CHECK(variant == 0 || item.place != 1);
            MG_CHECK(item.robot->m_CurrState == ROBOT_CARRYING);
            MG_CHECK(item.group >= 0 && item.group < MAX_LOGIC_GROUP);
            MG_CHECK(side.m_PlayerGroup[item.group].Order() == mpo_Attack);
            MG_CHECK(side.m_PlayerGroup[item.group].m_To == item.destination);
        }
        MG_CHECK(places == (variant == 0 ? std::set<int>({1, 2}) : std::set<int>({2, 3})));
        const std::array<int, 2> requested_places{cargo[0].place, cargo[1].place};
        recording.maintenance(draws, false, requested_places, before, resources_after, cooldown);
        const auto after_request = random::draw_count();
        factory->Maintenance();
        MG_CHECK(map.m_AD_Obj_cnt == 2 && random::draw_count() == after_request);
        recording.maintenance(after_request, true, requested_places);
        std::cout << session.metadata("neighboring-delivery-landings-v1") << " variant=" << variant << std::endl;
        const auto sample = [&](const std::array<std::optional<std::array<float, 3>>, 2> &positions,
                                const std::array<std::optional<float>, 2> &heights,
                                const std::array<int, 4> &observed_resources) {
            std::array<tests::carrier::Pair, 2> pairs{};
            for (int i = 0; i < 2; ++i) {
                const auto &item = cargo[i];
                pairs[i] = {item.carrier, item.robot, item.carrier_core.get(), item.robot_core.get(), item.place,
                    {item.destination.x, item.destination.y}, positions[i], heights[i],
                    tests::carrier::Progress{item.release, item.landing, item.retired, item.falling_samples}};
            }
            tests::carrier::Frame frame;
            frame.resources = observed_resources;
            const auto observation = tests::carrier::observe(session, pairs, frame);
            recording.snapshot(observation);
        };
        sample({}, {}, before);
        while (session.time() < 30000) {
            session.advance();
            const auto observed_resources = resources();
            MG_CHECK(observed_resources == before);
            MG_CHECK(session.robot(1).x == 100 && session.robot(1).y == 200);
            MG_CHECK(session.robot(3).x == 540 && session.robot(3).y == 100);
            std::array<std::optional<std::array<float, 3>>, 2> positions;
            std::array<std::optional<float>, 2> heights;
            int sampled = 0;
            for (auto &item : cargo) {
                MG_CHECK(item.robot_core->m_Object == item.robot);
                MG_CHECK(!item.robot->IsMustDie());
                heights[sampled] = finite(*item.robot);
                MG_CHECK(side.m_PlayerGroup[item.group].Order() == mpo_Attack);
                MG_CHECK(side.m_PlayerGroup[item.group].m_To == item.destination);
                if (item.carrier_core->m_Object) {
                    const auto &position = item.carrier->GetPos();
                    finite(position);
                    positions[sampled] = std::array<float, 3>{position.x, position.y, position.z};
                }
                else if (item.retired < 0) item.retired = session.time();
                if (item.release < 0 && item.robot->m_CurrState != ROBOT_CARRYING) {
                    item.release = session.time();
                    MG_CHECK(item.robot->m_CurrState == ROBOT_FALLING);
                    MG_CHECK(item.carrier_core->m_Object && !item.carrier->CarryingRobot());
                    item.released = D3DXVECTOR3(item.robot->m_PosX, item.robot->m_PosY, item.robot->GetMatrix()._43);
                    item.previous_z = item.released.z;
                    MG_CHECK(item.released.z > map.GetZ(item.released.x, item.released.y));
                }
                if (item.release >= 0 && item.robot->m_CurrState == ROBOT_FALLING) {
                    ++item.falling_samples;
                    MG_CHECK(item.robot->m_PosX == item.released.x && item.robot->m_PosY == item.released.y);
                    MG_CHECK(item.robot->GetMatrix()._43 <= item.previous_z);
                    item.previous_z = item.robot->GetMatrix()._43;
                    // Falling matrices retain their actual height; a later
                    // standing refresh must not conceal penetration below it.
                    MG_CHECK(item.previous_z >= 200 + slope * item.robot->m_PosX - 0.001f);
                }
                if (item.landing < 0 && item.release >= 0 && item.robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) {
                    item.landing = session.time();
                    if (item.landing != item.expected_landing)
                        std::cout << "place=" << item.place << " landing_ms=" << item.landing
                                  << " expected_landing_ms=" << item.expected_landing << std::endl;
                    MG_CHECK(item.landing == item.expected_landing);
                    // JoinToGroup can refresh the transform after contact:
                    // this checks standing height, not the contact threshold.
                    MG_CHECK(std::abs(item.robot->GetMatrix()._43 - (200 + slope * item.robot->m_PosX)) < 0.001f);
                }
                ++sampled;
            }
            sample(positions, heights, observed_resources);
        }
        MG_CHECK(map.m_AD_Obj_cnt == 0);
        std::array<std::array<float, 3>, 2> standing_poses{};
        int standing_sample = 0;
        for (const auto &item : cargo) {
            std::cout << "place=" << item.place << " release=" << item.release << " landing=" << item.landing
                      << " retirement=" << item.retired << " final=" << item.robot->m_PosX << ',' << item.robot->m_PosY
                      << " core=" << item.robot->GetMatrix()._41 << ',' << item.robot->GetMatrix()._42
                      << ',' << item.robot->GetMatrix()._43 << std::endl;
            MG_CHECK(item.release > 0 && item.landing > item.release && item.retired > item.landing && item.falling_samples > 1);
            MG_CHECK(!item.carrier_core->m_Object && item.robot_core->m_Object == item.robot);
            MG_CHECK(item.robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && item.robot->PLIsInPlace());
            MG_CHECK(item.robot->m_PosX == (item.destination.x + 2) * GLOBAL_SCALE_MOVE);
            MG_CHECK(item.robot->m_PosY == (item.destination.y + 2) * GLOBAL_SCALE_MOVE);
            // Without drawing, the last bounds-cache refresh may precede
            // arrival by up to 100 ms. One unit per axis is an empirical bound
            // for these profiles, not a bound on the final movement snap.
            const auto &pose = item.robot->GetMatrix();
            standing_poses[standing_sample++] = {pose._41, pose._42, pose._43};
            MG_CHECK(std::abs(pose._43 - (200 + slope * pose._41)) < 0.001f);
            MG_CHECK(std::abs(pose._41 - item.robot->m_PosX) < 1);
            MG_CHECK(std::abs(pose._42 - item.robot->m_PosY) < 1);
        }
        const float separation = std::hypot(cargo[0].robot->m_PosX - cargo[1].robot->m_PosX,
                                            cargo[0].robot->m_PosY - cargo[1].robot->m_PosY);
        MG_CHECK(separation >= 40 && separation <= 57);
        recording.finish(standing_poses, separation);
    }
}

}
