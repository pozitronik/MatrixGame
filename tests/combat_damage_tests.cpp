// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixConfig.hpp"
#include "Interface/CInterface.h"
#include "random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace {
constexpr std::array weapons{
    WEAPON_PLASMA, WEAPON_VOLCANO, WEAPON_HOMING_MISSILE, WEAPON_BOMB,
    WEAPON_FLAMETHROWER, WEAPON_BIGBOOM, WEAPON_LIGHTENING, WEAPON_LASER,
    WEAPON_GUN, WEAPON_ABLAZE, WEAPON_SHORTED, WEAPON_DEBRIS, WEAPON_REPAIR,
    WEAPON_CANNON0, WEAPON_CANNON1, WEAPON_CANNON2, WEAPON_CANNON3};
static_assert(weapons.size() == WEAPON_COUNT);
constexpr std::array modes{random::Mode::ParkMiller, random::Mode::LegacyCRT};
const D3DXVECTOR3 distant_hit{1000000, 1000000, 1000000};
const D3DXVECTOR3 direction{1, 0, 0};

struct DamageTables {
    std::array<SWeaponDamage, WEAPON_COUNT> robots, buildings, cannons;
    std::array<float, WEAPON_COUNT> radii;
    std::array<int, WEAPON_COUNT> cooldowns;
    float distance = CMatrixEffect::m_Dist2;
    DamageTables() {
        std::copy_n(g_Config.m_RobotDamages, WEAPON_COUNT, robots.begin());
        std::copy_n(g_Config.m_BuildingDamages, WEAPON_COUNT, buildings.begin());
        std::copy_n(g_Config.m_CannonDamages, WEAPON_COUNT, cannons.begin());
        std::copy_n(g_Config.m_WeaponRadius, WEAPON_COUNT, radii.begin());
        std::copy_n(g_Config.m_WeaponCooldown, WEAPON_COUNT, cooldowns.begin());
        for (int i = 0; i < WEAPON_COUNT; ++i) {
            g_Config.m_RobotDamages[i] = {i + 1, 2, 10};
            g_Config.m_BuildingDamages[i] = {i + 1, 2, 10};
            g_Config.m_CannonDamages[i] = {i + 1, 2, 10};
            g_Config.m_WeaponRadius[i] = 10;
            g_Config.m_WeaponCooldown[i] = 0;
        }
    }
    ~DamageTables() {
        std::copy(robots.begin(), robots.end(), g_Config.m_RobotDamages);
        std::copy(buildings.begin(), buildings.end(), g_Config.m_BuildingDamages);
        std::copy(cannons.begin(), cannons.end(), g_Config.m_CannonDamages);
        std::copy(radii.begin(), radii.end(), g_Config.m_WeaponRadius);
        std::copy(cooldowns.begin(), cooldowns.end(), g_Config.m_WeaponCooldown);
        CMatrixEffect::m_Dist2 = distance;
    }
};

struct World {
    struct Seed {
        explicit Seed(random::Mode mode) { random::seed(82, mode); }
    } seed;
    tests::CommandWorld world;
    DamageTables tables;
    tests::EnginePtr<CIFaceList> ui;
    CIFaceList *previous_interface = g_IFaceList;
    explicit World(random::Mode mode) : seed(mode) {
        ui = tests::make_engine_object<CIFaceList>();
        g_IFaceList = ui.get();
        world.map->m_Difficulty.k_damage_enemy_to_player = 1;
        world.map->m_Difficulty.k_friendly_fire = 1;
    }
    ~World() {
        ui.reset();
        g_IFaceList = previous_interface;
    }
};

struct Robot : CMatrixRobotAI {
    using CMatrixMapStatic::GetAblazeTTL;
    using CMatrixMapStatic::GetShortedTTL;
    using CMatrixMapStatic::IsAblaze;
    using CMatrixMapStatic::IsShorted;
    using CMatrixMapStatic::SetShortedTTL;
    Robot() { m_CurrState = ROBOT_SUCCESSFULLY_BUILD; m_Side = PLAYER_SIDE; }
    void health(float hp) { m_HitPoint = hp; }
    float health() const { return m_HitPoint; }
};

bool hit(CMatrixMapStatic &target, EWeapon weapon, int side = 2) {
    return target.Damage(weapon, distant_hit, direction, side, nullptr);
}

void robot_families() {
    for (auto mode : modes) {
        World world(mode);
        for (size_t i = 0; i < weapons.size(); ++i) {
            auto robot = tests::make_engine_object<Robot>();
            robot->InitMaxHitpoint(50);
            MG_CHECK(!hit(*robot, weapons[i]));
            const float expected = weapons[i] == WEAPON_REPAIR ? 50.0f : 49.0f - float(i);
            MG_CHECK(robot->health() == expected);
            MG_CHECK(robot->IsLiveRobot());
            MG_CHECK(robot->IsAblaze() == (weapons[i] == WEAPON_FLAMETHROWER));
            MG_CHECK(robot->IsShorted() == (weapons[i] == WEAPON_LIGHTENING));
        }
    }
}

void building_families() {
    for (auto mode : modes) {
        World world(mode);
        for (size_t i = 0; i < weapons.size(); ++i) {
            auto building = tests::make_engine_object<CMatrixBuilding>();
            building->m_Side = 2;
            building->InitMaxHitpoint(50);
            MG_CHECK(!hit(*building, weapons[i], PLAYER_SIDE));
            const float expected = weapons[i] == WEAPON_REPAIR ? 50.0f : 49.0f - float(i);
            MG_CHECK(building->GetHitPoint() == expected / 10.0f);
            MG_CHECK(building->State() != BUILDING_DIP);
        }
    }
}

void cannon_families() {
    for (auto mode : modes) {
        World world(mode);
        for (size_t i = 0; i < weapons.size(); ++i) {
            auto cannon = tests::make_engine_object<CMatrixCannon>();
            cannon->SetSide(2);
            cannon->InitMaxHitpoint(50);
            MG_CHECK(!hit(*cannon, weapons[i], PLAYER_SIDE));
            const float expected = weapons[i] == WEAPON_REPAIR ? 50.0f : 49.0f - float(i);
            MG_CHECK(cannon->GetHitPoint() == expected);
            MG_CHECK(cannon->m_CurrState != CANNON_DIP);
        }
    }
}

void difficulty() {
    for (auto mode : modes) {
        World world(mode);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {8, 4, 0};
        g_Config.m_BuildingDamages[Weap2Index(WEAPON_GUN)] = {8, 4, 0};
        g_Config.m_CannonDamages[Weap2Index(WEAPON_GUN)] = {8, 4, 0};
        struct Example { int victim, attacker; float enemy, friendly, remaining; };
        constexpr Example examples[] = {
            {1, 2, 0, 1, 50}, {1, 2, 0.5f, 1, 46}, {1, 2, 1, 1, 42}, {1, 2, 2, 1, 34},
            {1, 1, 2, 0, 50}, {1, 1, 2, 0.5f, 48}, {1, 1, 2, 1, 46}, {1, 1, 2, 2, 42},
            {2, 1, 2, 2, 42}, {2, 2, 2, 2, 46}, {1, 0, 2, 2, 34}, {2, 0, 2, 2, 42}};
        for (auto example : examples) {
            auto robot = tests::make_engine_object<Robot>();
            robot->m_Side = example.victim;
            robot->InitMaxHitpoint(50);
            world.world.map->m_Difficulty.k_damage_enemy_to_player = example.enemy;
            world.world.map->m_Difficulty.k_friendly_fire = example.friendly;
            MG_CHECK(!hit(*robot, WEAPON_GUN, example.attacker));
            MG_CHECK(robot->health() == example.remaining);
            auto building = tests::make_engine_object<CMatrixBuilding>();
            building->m_Side = example.victim;
            building->InitMaxHitpoint(50);
            MG_CHECK(!hit(*building, WEAPON_GUN, example.attacker));
            MG_CHECK(std::bit_cast<std::uint32_t>(building->GetHitPoint()) ==
                     std::bit_cast<std::uint32_t>(example.remaining / 10.0f));
            auto cannon = tests::make_engine_object<CMatrixCannon>();
            cannon->SetSide(example.victim);
            cannon->InitMaxHitpoint(50);
            MG_CHECK(!hit(*cannon, WEAPON_GUN, example.attacker));
            MG_CHECK(cannon->GetHitPoint() == example.remaining);
        }
    }
}

void minimum_health() {
    for (auto mode : modes) {
        World world(mode);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {4, 2, 10};
        for (auto initial : {9.0f, 10.0f, 11.0f}) {
            auto robot = tests::make_engine_object<Robot>();
            robot->InitMaxHitpoint(initial);
            MG_CHECK(!hit(*robot, WEAPON_GUN));
            MG_CHECK(robot->health() == (initial == 11 ? 7 : initial));
            // The threshold is tested before subtraction; it is not a floor clamp.
            const float once = robot->health();
            MG_CHECK(!hit(*robot, WEAPON_GUN));
            MG_CHECK(robot->health() == once);
        }
    }
}

void repair() {
    for (auto mode : modes) {
        World world(mode);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 100};
        auto robot = tests::make_engine_object<Robot>();
        robot->InitMaxHitpoint(50);
        robot->health(39);
        world.world.map->m_Difficulty.k_damage_enemy_to_player = 0;
        world.world.map->m_Difficulty.k_friendly_fire = 0;
        MG_CHECK(robot->NeedRepair());
        MG_CHECK(!hit(*robot, WEAPON_REPAIR, PLAYER_SIDE));
        MG_CHECK(robot->health() == 43);
        MG_CHECK(!hit(*robot, WEAPON_REPAIR, 2));
        MG_CHECK(robot->health() == 50);
        MG_CHECK(!robot->NeedRepair());
        MG_CHECK(!hit(*robot, WEAPON_REPAIR));
        MG_CHECK(robot->health() == 50);
        // A dead-state robot is never revived, even by a repair event.
        robot->m_CurrState = ROBOT_DIP;
        robot->health(0);
        MG_CHECK(hit(*robot, WEAPON_REPAIR));
        MG_CHECK(robot->health() == 0);
        MG_CHECK(hit(*robot, WEAPON_GUN));
        MG_CHECK(robot->health() == 0);
        robot->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
    }
}

void status_effects() {
    for (auto mode : modes) {
        World world(mode);
        auto robot = tests::make_engine_object<Robot>();
        robot->InitMaxHitpoint(50);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_FLAMETHROWER)] = {0, 0, 100};
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LIGHTENING)] = {0, 0, 100};
        for (int i = 0; i < 17; ++i) MG_CHECK(!hit(*robot, WEAPON_FLAMETHROWER));
        MG_CHECK(robot->IsAblaze());
        MG_CHECK(robot->GetAblazeTTL() == 5000);
        MG_CHECK(robot->health() == 50);
        MG_CHECK(!hit(*robot, WEAPON_FLAMETHROWER));
        MG_CHECK(robot->GetAblazeTTL() == 5000);
        for (int i = 0; i < 7; ++i) MG_CHECK(!hit(*robot, WEAPON_LIGHTENING));
        MG_CHECK(robot->IsShorted());
        MG_CHECK(robot->GetShortedTTL() == 3000);
        robot->SetShortedTTL(0);
        robot->m_LightProtect = 0.5f;
        MG_CHECK(!hit(*robot, WEAPON_LIGHTENING));
        MG_CHECK(robot->GetShortedTTL() == 250);
        for (int i = 0; i < 6; ++i) MG_CHECK(!hit(*robot, WEAPON_LIGHTENING));
        MG_CHECK(robot->GetShortedTTL() == 1500);
        robot->SetShortedTTL(0);
        robot->m_LightProtect = 1;
        MG_CHECK(!hit(*robot, WEAPON_LIGHTENING));
        MG_CHECK(robot->GetShortedTTL() == 0);
    }
}

void bomb_protection() {
    for (auto mode : modes) {
        World world(mode);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {8, 4, 0};
        for (float protection : {0.0f, 0.5f, 1.0f}) {
            auto robot = tests::make_engine_object<Robot>();
            robot->InitMaxHitpoint(50);
            robot->m_BombProtect = protection;
            MG_CHECK(!hit(*robot, WEAPON_BIGBOOM));
            MG_CHECK(robot->health() == (protection == 0 ? 42 : protection == 0.5f ? 46 : 50));
        }
    }
}

void building_death() {
    for (auto mode : modes) {
        World world(mode);
        auto building = tests::make_engine_object<CMatrixBuilding>();
        building->m_Side = 2;
        building->InitMaxHitpoint(4);
        building->AddLT();
        auto robot = tests::make_engine_object<Robot>();
        robot->SetBase(building.get());
        robot->AddLT();
        auto cannon = tests::make_engine_object<CMatrixCannon>();
        cannon->m_ParentBuilding = building.get();
        cannon->AddLT();
        g_Config.m_BuildingDamages[Weap2Index(WEAPON_GUN)] = {4, 2, 0};
        const int kills = world.world.map->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL);
        MG_CHECK(hit(*building, WEAPON_GUN, PLAYER_SIDE));
        MG_CHECK(building->State() == BUILDING_DIP);
        MG_CHECK(std::bit_cast<std::uint32_t>(building->GetHitPoint()) == 0xbdcccccdU);
        MG_CHECK(robot->GetBase() == nullptr);
        MG_CHECK(robot->IsMustDie());
        MG_CHECK(cannon->m_ParentBuilding == nullptr);
        MG_CHECK(world.world.map->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == kills + 1);
        MG_CHECK(hit(*building, WEAPON_REPAIR, PLAYER_SIDE));
        MG_CHECK(hit(*building, WEAPON_GUN, PLAYER_SIDE));
        MG_CHECK(std::bit_cast<std::uint32_t>(building->GetHitPoint()) == 0xbdcccccdU);
        MG_CHECK(world.world.map->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == kills + 1);
    }
}

void cannon_repair_state() {
    for (auto mode : modes) {
        World world(mode);
        auto cannon = tests::make_engine_object<CMatrixCannon>();
        cannon->SetSide(PLAYER_SIDE);
        cannon->InitMaxHitpoint(50);
        cannon->SetHitPoint(45);
        MG_CHECK(cannon->NeedRepair());
        cannon->m_CurrState = CANNON_UNDER_CONSTRUCTION;
        MG_CHECK(!cannon->NeedRepair());
        cannon->m_CurrState = CANNON_IDLE;
        cannon->SetInvulnerability();
        const auto before = random::draw_count();
        MG_CHECK(!hit(*cannon, WEAPON_GUN));
        MG_CHECK(!hit(*cannon, WEAPON_REPAIR));
        MG_CHECK(cannon->GetHitPoint() == 45);
        MG_CHECK(random::draw_count() == before);
        cannon->ResetInvulnerability();
        MG_CHECK(!hit(*cannon, WEAPON_REPAIR, PLAYER_SIDE));
        MG_CHECK(cannon->GetHitPoint() == 47);
        MG_CHECK(!hit(*cannon, WEAPON_REPAIR, 2));
        MG_CHECK(cannon->GetHitPoint() == 50);
        MG_CHECK(!cannon->NeedRepair());
        cannon->m_CurrState = CANNON_DIP;
        MG_CHECK(hit(*cannon, WEAPON_REPAIR));
        MG_CHECK(cannon->GetHitPoint() == 50);
        cannon->m_CurrState = CANNON_IDLE;
    }
}

void building_repair() {
    for (auto mode : modes) {
        World world(mode);
        auto building = tests::make_engine_object<CMatrixBuilding>();
        building->m_Side = PLAYER_SIDE;
        building->InitMaxHitpoint(50);
        g_Config.m_BuildingDamages[Weap2Index(WEAPON_GUN)] = {10, 2, 0};
        g_Config.m_BuildingDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 100};
        MG_CHECK(!hit(*building, WEAPON_GUN));
        MG_CHECK(building->GetHitPoint() == 4);
        MG_CHECK(building->NeedRepair());
        world.world.map->m_Difficulty.k_damage_enemy_to_player = 0;
        world.world.map->m_Difficulty.k_friendly_fire = 0;
        MG_CHECK(!hit(*building, WEAPON_REPAIR, PLAYER_SIDE));
        MG_CHECK(std::bit_cast<std::uint32_t>(building->GetHitPoint()) == 0x408ccccdU); // 4.4
        MG_CHECK(!hit(*building, WEAPON_REPAIR));
        MG_CHECK(building->GetHitPoint() == 5);
        MG_CHECK(!building->NeedRepair());
        MG_CHECK(!hit(*building, WEAPON_REPAIR));
        MG_CHECK(building->GetHitPoint() == 5);
    }
}

struct WeaponDelete { void operator()(CMatrixEffectWeapon *w) const { w->Release(); } };
using WeaponPtr = std::unique_ptr<CMatrixEffectWeapon, WeaponDelete>;
struct HitRecord {
    int calls = 0;
    CMatrixMapStatic *target = nullptr;
    DWORD flags = 0;
    static void record(CMatrixMapStatic *target, const D3DXVECTOR3 &, uintptr_t user, DWORD flags) {
        auto &record = *reinterpret_cast<HitRecord *>(user);
        ++record.calls;
        record.target = target;
        record.flags = flags;
    }
};
WeaponPtr weapon(EWeapon type, HitRecord &record) {
    return WeaponPtr(static_cast<CMatrixEffectWeapon *>(CMatrixEffect::CreateWeapon(
        distant_hit, direction, reinterpret_cast<uintptr_t>(&record), HitRecord::record, type)));
}

void projectile_range_callback() {
    for (auto mode : modes) {
        World world(mode);
        for (auto type : {WEAPON_HOMING_MISSILE, WEAPON_GUN, WEAPON_CANNON0, WEAPON_CANNON1, WEAPON_CANNON3}) {
            g_Config.m_RobotDamages[Weap2Index(type)] = {4, 2, 0};
            HitRecord record;
            auto projectile = weapon(type, record);
            MG_CHECK(projectile->GetWeaponDist() == 10);
            auto robot = tests::make_engine_object<Robot>();
            robot->InitMaxHitpoint(50);
            for (auto distance : {99.0f, 100.0f, 101.0f}) {
                robot->health(50);
                CMatrixEffect::m_Dist2 = distance;
                CMatrixEffectWeapon::WeaponHit(robot.get(), distant_hit,
                    reinterpret_cast<uintptr_t>(projectile.get()), 0);
                MG_CHECK(robot->health() == (distance <= 100 ? 46 : 50));
                MG_CHECK(record.target == robot.get());
                MG_CHECK(record.flags == (distance <= 100 ? FEHF_DAMAGE_ROBOT : 0));
            }
            MG_CHECK(record.calls == 3);
        }
    }
}

void projectile_owner_retirement() {
    for (auto mode : modes) {
        World world(mode);
        HitRecord record;
        auto projectile = weapon(WEAPON_GUN, record);
        auto owner = tests::make_engine_object<Robot>();
        owner->RNoNeed(0xffffffff); // Identity-only core; this fixture has no mesh-derived bounds.
        owner->AddLT();
        projectile->SetOwner(owner.get());
        MG_CHECK(projectile->GetOwner() == owner.get());
        MG_CHECK(projectile->GetSideStorage() == PLAYER_SIDE);
        owner.reset();
        MG_CHECK(projectile->GetOwner() == nullptr);
        MG_CHECK(projectile->GetSideStorage() == PLAYER_SIDE);
        auto target = tests::make_engine_object<Robot>();
        target->InitMaxHitpoint(50);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {8, 2, 0};
        CMatrixEffect::m_Dist2 = 0;
        CMatrixEffectWeapon::WeaponHit(target.get(), distant_hit,
            reinterpret_cast<uintptr_t>(projectile.get()), 0);
        MG_CHECK(target->health() == 48); // Retained side still applies friendly-fire damage.
        MG_CHECK(record.calls == 1);
        MG_CHECK(record.flags == FEHF_DAMAGE_ROBOT);
    }
}

const tests::Case cases[] = {
    {"game.combat.robot_families", robot_families},
    {"game.combat.building_families", building_families},
    {"game.combat.cannon_families", cannon_families},
    {"game.combat.difficulty", difficulty},
    {"game.combat.minimum_health", minimum_health},
    {"game.combat.repair", repair},
    {"game.combat.status_effects", status_effects},
    {"game.combat.bomb_protection", bomb_protection},
    {"game.combat.building_death", building_death},
    {"game.combat.cannon_repair_state", cannon_repair_state},
    {"game.combat.building_repair", building_repair},
    {"game.combat.projectile_range_callback", projectile_range_callback},
    {"game.combat.projectile_owner_retirement", projectile_owner_retirement}};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
