// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "Interface/CConstructor.h"
#include "stupid_logger.hpp"

#include <algorithm>
#include <array>
#include <iterator>

// The entry point supplies configuration and heap services without game startup.
CMatrixConfig g_Config;
Base::CHeap *g_MatrixHeap = nullptr;
logger_type lgr{std::cerr};

namespace {

using Cost = std::array<int, MAX_RESOURCES>;
constexpr Cost chassis_cost{3, 5, 7, 11};
constexpr Cost armor_cost{13, 17, 19, 23};
constexpr Cost gun_cost{29, 31, 37, 41};
constexpr Cost cannon_cost{43, 47, 53, 59};
constexpr std::array<Cost, 4> head_costs{{
    {101, 103, 107, 109},
    {211, 223, 227, 229},
    {307, 311, 313, 317},
    {401, 409, 419, 421},
}};

Cost plus(Cost left, const Cost &right) {
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        left[resource] += right[resource];
    }
    return left;
}

void check_cost(const int (&actual)[MAX_RESOURCES], const Cost &expected) {
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        if (actual[resource] != expected[resource]) {
            std::cerr << "Resource " << resource << ": expected " << expected[resource]
                      << ", got " << actual[resource] << '\n';
        }
        MG_CHECK(actual[resource] == expected[resource]);
    }
}

struct Definitions {
    CBlockPar input;
    std::array<SRobotWeaponMatrix, ROBOT_ARMOR_CNT> capacities{};

    Definitions() {
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        std::fill(std::begin(g_Config.m_WeaponStrengthAI), std::end(g_Config.m_WeaponStrengthAI), 0.0f);
        set_price(CHASSIS1_TITAN, chassis_cost);
        // The legacy definition token "1" selects Monostack, armor kind six.
        set_price(ARMOR6_TITAN, armor_cost);
        set_price(WEAPON1_TITAN, gun_cost);
        set_price(WEAPON1_TITAN + 4, cannon_cost);
        // Each head price differs from the weapon price at the same kind index.
        set_price(WEAPON1_TITAN + 8, Cost{61, 67, 71, 73});
        set_price(WEAPON1_TITAN + 12, Cost{79, 83, 89, 97});
        for (int head = 0; head < head_costs.size(); ++head) {
            set_price(HEAD1_TITAN + head * MAX_RESOURCES, head_costs[head]);
        }
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_MACHINEGUN] = 10.0f;
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_CANNON] = 5.0f;
        capacities[RUK_ARMOR_MONOSTACK - 1].common = 2;
    }

    ~Definitions() { SSpecialBot::ClearAIRobotType(); }

    void set_price(int first_resource, const Cost &price) {
        std::copy(price.begin(), price.end(), std::begin(g_Config.m_Price) + first_resource);
    }

    void load() { SSpecialBot::LoadAIRobotType(input, capacities); }
};

void head_price() {
    constexpr std::array<const wchar_t *, 8> aliases{
        L"Strength", L"S", L"Dynamo", L"D", L"Locator", L"L", L"Firewall", L"F"};
    Definitions definitions;
    for (int row = 0; row < aliases.size(); ++row) {
        definitions.input.ParAdd(std::to_wstring(row + 10), std::wstring(L"Pneumatic, 1, G, ") + aliases[row]);
    }
    definitions.load();
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == aliases.size());
    for (int row = 0; row < aliases.size(); ++row) {
        const auto &bot = SSpecialBot::m_AIRobotTypeList[row];
        const auto head = row / 2;
        check_cost(bot.m_Resources, plus(Cost{45, 53, 63, 75}, head_costs[head]));
        check_cost(bot.m_Head.m_Price.m_Resources, head_costs[head]);
        MG_CHECK(bot.m_Head.m_nType == MRT_HEAD);
        MG_CHECK(bot.m_Head.m_nKind == static_cast<ERobotUnitKind>(head + 1));
        MG_CHECK(bot.m_Chassis.m_nType == MRT_CHASSIS);
        MG_CHECK(bot.m_Chassis.m_nKind == RUK_CHASSIS_PNEUMATIC);
        MG_CHECK(bot.m_Armor.m_Unit.m_nType == MRT_ARMOR);
        MG_CHECK(bot.m_Armor.m_Unit.m_nKind == RUK_ARMOR_MONOSTACK);
        MG_CHECK(bot.m_Weapon[0].m_Unit.m_nType == MRT_WEAPON);
        MG_CHECK(bot.m_Weapon[0].m_Unit.m_nKind == RUK_WEAPON_MACHINEGUN);
        MG_CHECK(bot.m_Pripor == row + 10);
        MG_CHECK(bot.m_Strength == 10.0f);
    }
}

void no_head() {
    Definitions definitions;
    definitions.input.ParAdd(L"1", L"P,1,G");
    definitions.load();
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 1);
    const auto &bot = SSpecialBot::m_AIRobotTypeList[0];
    MG_CHECK(bot.m_Head.m_nType == MRT_HEAD);
    MG_CHECK(bot.m_Head.m_nKind == RUK_UNKNOWN);
    check_cost(bot.m_Head.m_Price.m_Resources, Cost{});
    check_cost(bot.m_Resources, Cost{45, 53, 63, 75});
    definitions.input.Clear();
    SSpecialBot::LoadAIRobotType(definitions.input, {});
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 0);
    SSpecialBot::ClearAIRobotType();
    MG_CHECK(SSpecialBot::m_AIRobotTypeList == nullptr);
}

void strength_order() {
    Definitions definitions;
    definitions.input.ParAdd(L"19", L"Pneumatic,1,G,Strength");
    definitions.input.ParAdd(L"7", L"P,1,GG,L");
    definitions.input.ParAdd(L"31", L"P,1,C,Dynamo");
    definitions.load();
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 3);
    const auto *bots = SSpecialBot::m_AIRobotTypeList;
    MG_CHECK(bots[0].m_Pripor == 7 && bots[0].m_Strength == 20.0f);
    MG_CHECK(bots[1].m_Pripor == 19 && bots[1].m_Strength == 10.0f);
    MG_CHECK(bots[2].m_Pripor == 31 && bots[2].m_Strength == 5.0f);
    MG_CHECK(bots[0].m_Head.m_nKind == RUK_HEAD_LOCKATOR);
    MG_CHECK(bots[1].m_Head.m_nKind == RUK_HEAD_BLOCKER);
    MG_CHECK(bots[2].m_Head.m_nKind == RUK_HEAD_DYNAMO);
    MG_CHECK(bots[0].m_Armor.m_MaxCommonWeaponCnt == 2);
    MG_CHECK(bots[0].m_Armor.m_MaxExtraWeaponCnt == 0);
    check_cost(bots[0].m_Resources, plus(plus(Cost{45, 53, 63, 75}, gun_cost), head_costs[2]));
    check_cost(bots[2].m_Resources, plus(plus(plus(chassis_cost, armor_cost), cannon_cost), head_costs[1]));
}

void capacity_bounds() {
    Definitions definitions;
    definitions.input.ParAdd(L"1", L"P,1,G,S");
    definitions.load();
    MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 1);
    bool rejected = false;
    try {
        // Monostack is kind six; the explicit table omits its entry.
        SSpecialBot::LoadAIRobotType(definitions.input,
                std::span<const SRobotWeaponMatrix>(definitions.capacities.data(), ROBOT_ARMOR_CNT - 1));
    }
    catch (const Base::CException &error) {
        rejected = true;
        MG_CHECK(error.Info().find(L"missing armor capacity: 6") != std::wstring::npos);
    }
    MG_CHECK(rejected);
}

constexpr tests::Case cases[]{
    {"game.ai.head_price", head_price},
    {"game.ai.no_head", no_head},
    {"game.ai.strength_order", strength_order},
    {"game.ai.capacity_bounds", capacity_bounds},
};

}  // namespace

int main(int argc, char **argv) {
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result;
    {
        DTRACE();
        result = tests::run(argc, argv, cases);
    }
#ifdef _DEBUG
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Tracked engine allocations remain after the AI case\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
