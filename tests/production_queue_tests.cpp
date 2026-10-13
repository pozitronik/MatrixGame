// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CConstructor.h"

#include <array>
#include <algorithm>

namespace {
using Cost = std::array<int, MAX_RESOURCES>;
constexpr Cost wheel_price{11, 17, 23, 31};

struct World {
    tests::CommandWorld world;
    CIFaceList *previous_ui = g_IFaceList;
    tests::EnginePtr<CIFaceList> ui = tests::make_engine_object<CIFaceList>();
    tests::EnginePtr<CMatrixBuilding> base;

    World() {
        g_IFaceList = ui.get();
        g_Config.SetDefaults();
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        std::copy(wheel_price.begin(), wheel_price.end(), &g_Config.m_Price[CHASSIS1_TITAN]);
        base = tests::make_engine_object<CMatrixBuilding>();
        base->m_Side = PLAYER_SIDE;
        base->m_State = BASE_CLOSED;
        balances({100, 200, 300, 400});
    }
    ~World() {
        base.reset();
        ui.reset();
        g_IFaceList = previous_ui;
    }
    CMatrixSideUnit &side() { return *world.map->GetPlayerSide(); }
    void balances(Cost values) {
        for (int i = 0; i < MAX_RESOURCES; ++i) side().SetResourceAmount(static_cast<ERes>(i), values[i]);
    }
    void expect(Cost values) {
        for (int i = 0; i < MAX_RESOURCES; ++i) MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(i)) == values[i]);
    }
    tests::EnginePtr<CMatrixRobotAI> make_robot() {
        auto robot = tests::make_engine_object<CMatrixRobotAI>();
        robot->m_Side = PLAYER_SIDE;
        robot->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_PNEUMATIC);
        robot->SetBase(base.get());
        return robot;
    }
    CMatrixRobotAI *enqueue() {
        auto robot = make_robot();
        auto *result = robot.get();
        base->m_BS.AddItem(result);
        MG_CHECK(base->m_BS.GetRobotsCnt() == base->m_BS.GetItemsCnt());
        robot.release();
        return result;
    }
};

void queue_order_and_refunds() {
    World w;
    auto *first = w.enqueue();
    auto *second = w.enqueue();
    auto *third = w.enqueue();
    w.expect({100, 200, 300, 400}); // AddItem takes ownership; callers debit resources.
    MG_CHECK(w.base->m_BS.GetTopItem() == first);
    MG_CHECK(first->m_NextStackItem == second && second->m_NextStackItem == third);
    MG_CHECK(w.base->m_BS.DeleteItem(2) == 2);
    MG_CHECK(first->m_NextStackItem == third && third->m_PrevStackItem == first);
    w.expect({111, 217, 323, 431});
    MG_CHECK(w.base->m_BS.DeleteItem(0) == 0);
    MG_CHECK(w.base->m_BS.DeleteItem(9) == 0);
    MG_CHECK(w.base->m_BS.GetItemsCnt() == 2);
    w.expect({111, 217, 323, 431});
    w.base->m_BS.DeleteItem(first);
    MG_CHECK(w.base->m_BS.GetTopItem() == third);
    w.expect({122, 234, 346, 462});
    w.base->m_BS.ClearStack();
    MG_CHECK(w.base->m_BS.GetTopItem() == nullptr);
    MG_CHECK(w.base->m_BS.GetItemsCnt() == 0);
    w.expect({133, 251, 369, 493});
    w.base->m_BS.ClearStack();
    w.expect({133, 251, 369, 493});
}

void queue_capacity() {
    World w;
    w.base->m_BS.AddItem(nullptr);
    MG_CHECK(w.base->m_BS.GetItemsCnt() == 0);
    for (int i = 0; i < MAX_STACK_UNITS; ++i) w.enqueue();
    MG_CHECK(w.base->m_BS.IsMaxItems());
    auto rejected = w.make_robot();
    w.base->m_BS.AddItem(rejected.get());
    MG_CHECK(w.base->m_BS.GetItemsCnt() == MAX_STACK_UNITS);
    MG_CHECK(rejected->m_NextStackItem == nullptr && rejected->m_PrevStackItem == nullptr);
    w.expect({100, 200, 300, 400});
    w.base->m_BS.ClearStack();
    MG_CHECK(!w.base->m_BS.IsMaxItems());
    w.expect({100 + MAX_STACK_UNITS * 11, 200 + MAX_STACK_UNITS * 17,
              300 + MAX_STACK_UNITS * 23, 400 + MAX_STACK_UNITS * 31});
}

void refund_owner_and_cap() {
    World w;
    w.enqueue();
    w.base->m_Side = 2; // Refund follows the queued unit, not the current base owner.
    w.balances({8995, 8980, 9000, 0});
    w.base->m_BS.ClearStack();
    w.expect({9000, 8997, 9000, 31});
}

void construction_price() {
    World w;
    auto constructor = tests::make_engine_object<CConstructor>();
    auto *robot = constructor->GetRenderBot();
    Cost actual{};
    constructor->GetConstructionPrice(actual.data());
    MG_CHECK(actual == Cost{});
    robot->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_PNEUMATIC);
    constructor->GetConstructionPrice(actual.data());
    MG_CHECK(actual == wheel_price);
    const Cost head{3, 5, 7, 9};
    std::copy(head.begin(), head.end(), &g_Config.m_Price[HEAD1_TITAN]);
    robot->UnitInsert(1, MRT_HEAD, RUK_HEAD_BLOCKER);
    constructor->GetConstructionPrice(actual.data());
    MG_CHECK((actual == Cost{14, 22, 30, 40}));
    w.balances(actual);
    MG_CHECK(w.side().IsEnoughResources(actual.data()));
    for (int i = 0; i < MAX_RESOURCES; ++i) {
        w.balances(actual);
        w.side().AddResourceAmount(static_cast<ERes>(i), -1);
        MG_CHECK(!w.side().IsEnoughResources(actual.data()));
    }
}

void income_ownership() {
    World w;
    w.base->AddLT();
    w.side().SetResourceForceUp(150);
    auto factory = tests::make_engine_object<CMatrixBuilding>();
    factory->m_Side = PLAYER_SIDE;
    factory->m_Kind = BUILDING_TITAN;
    factory->AddLT();
    for (int i = 0; i < MAX_RESOURCES; ++i) {
        int base_income = -1, factory_income = -1;
        w.side().GetResourceIncome(base_income, factory_income, static_cast<ERes>(i));
        MG_CHECK(base_income == RESOURCES_INCOME_BASE * 150 / 100);
        MG_CHECK(factory_income == (i == TITAN ? RESOURCES_INCOME : 0));
    }
    factory->m_Side = 2;
    int base_income = -1, factory_income = -1;
    w.side().GetResourceIncome(base_income, factory_income, TITAN);
    MG_CHECK(base_income == RESOURCES_INCOME_BASE * 150 / 100 && factory_income == 0);
    w.base->m_Side = 0;
    w.side().GetResourceIncome(base_income, factory_income, TITAN);
    MG_CHECK(base_income == 0 && factory_income == 0);
    w.expect({100, 200, 300, 400}); // Reporting income does not grant it.
}

void reinforcement_gates() {
    World w;
    auto &map = *w.world.map;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        random::seed(83, mode);
        MG_CHECK(!map.MaintenanceDisabled());
        map.m_Difficulty.k_time_before_maintenance = 1.5f;
        g_Config.m_MaintenanceTime = 1000;
        map.InitMaintenanceTime();
        MG_CHECK(map.BeforeMaintenanceTime() == 1500);
        w.base->Maintenance();
        MG_CHECK(map.BeforeMaintenanceTime() == 1500 && random::draw_count() == 0);
        map.SetMaintenanceTime(1);
        w.base->Maintenance();
        MG_CHECK(map.BeforeMaintenanceTime() == 1 && random::draw_count() == 0);
        map.SetMaintenanceTime(0);
        w.base->m_Side = 0;
        w.base->Maintenance();
        MG_CHECK(map.BeforeMaintenanceTime() == 0 && random::draw_count() == 0);
        w.base->m_Side = PLAYER_SIDE;
        w.expect({100, 200, 300, 400});
        MG_CHECK(w.base->m_BS.GetItemsCnt() == 0);
    }
}

void turret_progress_and_cancel() {
    World w;
    g_Config.m_Timings[UNIT_TURRET] = 100;
    const Cost refund{2, 3, 5, 7};
    std::copy(refund.begin(), refund.end(), g_Config.m_CannonsProps[0].m_Resources);
    auto *first = w.world.map->StaticAdd<CMatrixCannon>();
    first->m_Num = 1;
    first->SetSide(PLAYER_SIDE);
    first->m_CurrState = CANNON_UNDER_CONSTRUCTION;
    first->InitMaxHitpoint(1000);
    first->SetHitPoint(0);
    auto *second = w.world.map->StaticAdd<CMatrixCannon>();
    second->m_Num = 1;
    second->SetSide(PLAYER_SIDE);
    second->m_CurrState = CANNON_UNDER_CONSTRUCTION;
    second->InitMaxHitpoint(1000);
    second->SetHitPoint(0);
    w.base->m_BS.AddItem(first);
    w.base->m_BS.AddItem(second);
    MG_CHECK(w.base->m_BS.GetRobotsCnt() == 0);
    w.base->m_BS.TickTimer(25);
    MG_CHECK(first->GetHitPoint() == 250 && second->GetHitPoint() == 0);
    w.expect({100, 200, 300, 400});
    MG_CHECK(w.base->m_BS.DeleteItem(1) == 1);
    w.expect({102, 203, 305, 407});
    w.base->m_BS.TickTimer(10);
    MG_CHECK(second->GetHitPoint() == 100); // Canceled head resets elapsed time.
    MG_CHECK(second->m_CurrState == CANNON_UNDER_CONSTRUCTION);
    w.base->m_BS.TickTimer(89);
    MG_CHECK(second->GetHitPoint() == 990 && w.base->m_BS.GetItemsCnt() == 1);
    w.base->m_BS.TickTimer(1);
    MG_CHECK(second->GetHitPoint() == 1000 && second->m_CurrState == CANNON_IDLE);
    MG_CHECK(w.base->m_BS.GetItemsCnt() == 0);
    w.expect({102, 203, 305, 407});
    w.world.map->StaticDelete(second);
}

void income_tick_boundaries() {
    const std::array<EBuildingType, 4> kinds{BUILDING_TITAN, BUILDING_ELECTRONIC, BUILDING_ENERGY, BUILDING_PLASMA};
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        World w;
        w.base->m_Kind = kinds[resource];
        for (auto timing : {RESOURCE_TITAN, RESOURCE_ELECTRONICS, RESOURCE_ENERGY, RESOURCE_PLASMA}) {
            g_Config.m_Timings[timing] = 100;
        }
        w.balances({0, 0, 0, 0});
        w.base->LogicTakt(99);
        w.expect({0, 0, 0, 0});
        w.base->LogicTakt(1);
        Cost expected{};
        expected[resource] = 10;
        w.expect(expected);
        w.base->LogicTakt(250); // A long boundary grants one payout and discards surplus time.
        expected[resource] = 20;
        w.expect(expected);
        w.base->LogicTakt(99);
        w.expect(expected);
        w.base->LogicTakt(1);
        expected[resource] = 30;
        w.expect(expected);
        w.base->m_Side = 0;
        w.base->LogicTakt(100);
        w.expect(expected);
    }
    World w;
    w.side().SetResourceForceUp(150);
    g_Config.m_Timings[RESOURCE_BASE] = 100;
    w.balances({8998, 8996, 0, 100});
    w.base->LogicTakt(100);
    w.expect({9000, 9000, 4, 104}); // Integer truncation precedes balance saturation.
}

void ai_and_constructor_prices() {
    World w;
    struct DefinitionsGuard { ~DefinitionsGuard() { SSpecialBot::ClearAIRobotType(); } } guard;
    const Cost armor{41, 43, 47, 53}, weapon{59, 61, 67, 71}, head{73, 79, 83, 89};
    std::copy(armor.begin(), armor.end(), &g_Config.m_Price[ARMOR6_TITAN]);
    std::copy(weapon.begin(), weapon.end(), &g_Config.m_Price[WEAPON1_TITAN]);
    std::copy(head.begin(), head.end(), &g_Config.m_Price[HEAD1_TITAN]);
    CBlockPar definitions;
    definitions.ParAdd(L"1", L"P,1,G,S");
    definitions.ParAdd(L"2", L"P,1,G");
    std::array<SRobotWeaponMatrix, ROBOT_ARMOR_CNT> capacities{};
    capacities[RUK_ARMOR_MONOSTACK - 1].common = 1;
    SSpecialBot::LoadAIRobotType(definitions, capacities);
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
    for (int i = 0; i < SSpecialBot::m_AIRobotTypeCnt; ++i) {
        const auto &bot = SSpecialBot::m_AIRobotTypeList[i];
        auto constructor = tests::make_engine_object<CConstructor>();
        auto *robot = constructor->GetRenderBot();
        robot->m_Side = PLAYER_SIDE;
        robot->UnitInsert(0, MRT_CHASSIS, bot.m_Chassis.m_nKind);
        robot->UnitInsert(1, MRT_ARMOR, bot.m_Armor.m_Unit.m_nKind);
        robot->UnitInsert(2, MRT_WEAPON, bot.m_Weapon[0].m_Unit.m_nKind);
        if (bot.m_Head.m_nKind) robot->UnitInsert(3, MRT_HEAD, bot.m_Head.m_nKind);
        Cost price{};
        constructor->GetConstructionPrice(price.data());
        const Cost expected = bot.m_Head.m_nKind ? Cost{184, 200, 220, 244} : Cost{111, 121, 137, 155};
        MG_CHECK(price == expected);
        for (int r = 0; r < MAX_RESOURCES; ++r) MG_CHECK(bot.m_Resources[r] == expected[r]);
        w.balances({0, 0, 0, 0});
        w.base->m_BS.ReturnRobotResources(robot);
        w.expect(expected);
    }
}

constexpr tests::Case cases[]{
    {"game.production.queue_order", queue_order_and_refunds},
    {"game.production.capacity", queue_capacity},
    {"game.production.refund_owner", refund_owner_and_cap},
    {"game.production.price", construction_price},
    {"game.production.income", income_ownership},
    {"game.production.reinforcement_gates", reinforcement_gates},
    {"game.production.turret_progress", turret_progress_and_cancel},
    {"game.production.income_ticks", income_tick_boundaries},
    {"game.production.ai_constructor_prices", ai_and_constructor_prices},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
