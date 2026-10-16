// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "Logic/MatrixEnvironment.h"
#include "PlaceContainment.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
using namespace tests;
using namespace tests::battle;
struct Containment {
    explicit Containment(bool original) { PlaceContainment::set_original(original); }
    ~Containment() { PlaceContainment::set_original(PlaceContainment::original_by_default()); }
};
struct CoreRelease { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, CoreRelease>;
enum class Refuge { Free, Reserved, Unreachable };
const char *name(Refuge refuge) {
    switch (refuge) {
    case Refuge::Free: return "free";
    case Refuge::Reserved: return "reserved";
    case Refuge::Unreachable: return "unreachable";
    }
    throw std::runtime_error("Unknown refuge profile");
}
std::array<CMatrixRobotAI *, 4> robots() {
    std::array<CMatrixRobotAI *, 4> result{};
    int count = 0;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsLiveRobot()) continue;
        MG_CHECK(count < 4);
        result[count++] = object->AsRobot();
    }
    MG_CHECK(count == 4);
    return result;
}
void position(CMatrixRobotAI &robot, float x, float y) {
    robot.m_PosX = x; robot.m_PosY = y; robot.MapPosCalc();
    robot.RChange(MR_Matrix); robot.RNeed(MR_Matrix); robot.JoinToGroup();
}
float distance(const CMatrixRobotAI &robot, float x, float y) {
    return std::hypot(robot.m_PosX - x, robot.m_PosY - y);
}
std::vector<std::string> journey(Refuge refuge, bool detonate, bool original, random::Mode mode, int step) {
    Containment containment(original);
    BattleCombatAssets assets;
    Profile profile{mode, 84, step, 5000, false, false};
    profile.layout = NavigationLayout::Open;
    Session session(profile);
    assets.install();
    const auto units = robots();
    auto *cover = units[0], *escaping = units[1], *bomber = units[2];
    position(*cover, 120, 100); position(*escaping, 180, 100); position(*bomber, 100, 100);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 250;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    // The cover weapon supplies perception range. With two unarmed opponents,
    // a bomb-only robot's zero ordinary fire range loses this observation.
    arm_battle_robot(*cover, RUK_WEAPON_LASER);
    arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    for (auto *unit : units) {
        unit->InitMaxHitpoint(100);
        // Component-level initial intent: no player group replaces these low-level
        // orders. Subsequent movement, perception and side ticks remain production.
        unit->SetGroupLogic(-1);
        if (unit != escaping) unit->SetMaxSpeed(0);
    }
    CoreLease core(bomber->GetCore(DEBUG_CALL_INFO));
    bomber->GetEnv()->AddToList(cover); // Initial historical observation.
    if (refuge == Refuge::Reserved) cover->MoveTo(48, 8);
    if (refuge == Refuge::Unreachable) {
        auto &start = g_MatrixMap->m_RN.m_Place[0];
        for (int edge = 0; edge < start.m_NearCnt; ++edge)
            start.m_NearMove[edge] = 1 << (escaping->m_Unit[0].u1.s1.m_Kind - 1);
    }
    const CPoint intended(20, 20);
    const CPoint expected = refuge == Refuge::Unreachable ? intended :
        refuge == Refuge::Reserved && !original ? CPoint(8, 48) : CPoint(48, 8);
    escaping->MoveTo(intended.x, intended.y);
    const auto initial_draws = random::draw_count();
    g_MatrixMap->GetPlayerSide()->EscapeFromBomb();
    MG_CHECK(random::draw_count() == initial_draws);
    CPoint destination, previous;
    MG_CHECK(escaping->GetMoveToCoords(destination) && destination == expected);
    MG_CHECK(escaping->GetReturnCoords(previous) == (refuge != Refuge::Unreachable));
    if (refuge != Refuge::Unreachable) MG_CHECK(previous == intended);
    MG_CHECK(!cover->GetReturnCoords(previous));
    const auto header = session.metadata("bomb-escape-journey-v1");
    MG_CHECK(header.find(original ? R"("place_containment":"Original")" : R"("place_containment":"Corrected")") != std::string::npos);
    std::cout << header << "\nrefuge=" << name(refuge) << " detonate=" << detonate << std::endl;
    std::vector<std::string> states{session.snapshot()};
    int safe_at = 0, refuge_at = 0, first_return = 0, second_escape = 0, cleared_at = 0;
    int damage_at = 0, arrived_at = 0, retired_at = 0;
    const bool observe_rediversion = refuge == Refuge::Free && !detonate;
    float maximum_distance = distance(*escaping, 100, 100);
    while (session.time() < 45000) {
        session.advance();
        MG_CHECK(escaping->IsLiveRobot() && cover->IsLiveRobot());
        MG_CHECK(escaping->GetGroupLogic() == -1 && cover->GetGroupLogic() == -1);
        MG_CHECK(!cover->GetReturnCoords(previous));
        MG_CHECK(!cover->FindOrderLikeThat(ROT_FIRE));
        const bool returning = escaping->GetReturnCoords(previous);
        const bool moving = escaping->GetMoveToCoords(destination);
        if (returning) MG_CHECK(previous == intended);
        if (refuge == Refuge::Unreachable) {
            MG_CHECK(!returning);
            if (moving) MG_CHECK(destination == intended);
        }
        maximum_distance = std::max(maximum_distance, distance(*escaping, 100, 100));
        if (!safe_at && distance(*escaping, 100, 100) > 330) safe_at = session.time();
        if (refuge != Refuge::Unreachable && !refuge_at &&
            distance(*escaping, float((expected.x + 2) * 10), float((expected.y + 2) * 10)) < 1)
            refuge_at = session.time();
        if (refuge != Refuge::Unreachable && !first_return && !returning && moving && destination == intended)
            first_return = session.time();
        if (first_return && returning && !second_escape) second_escape = session.time();
        if (!cleared_at) {
            MG_CHECK(core->m_Object == bomber && bomber->IsLiveRobot());
            MG_CHECK(bomber->GetEnv()->SearchEnemy(cover));
            MG_CHECK(escaping->GetHitPoint() == 10 && cover->GetHitPoint() == 10);
            const bool clear_now = refuge == Refuge::Unreachable ? session.time() >= 1200 :
                observe_rediversion ? second_escape != 0 : safe_at != 0;
            if (clear_now) {
                cleared_at = session.time();
                if (detonate) {
                    bomber->BigBoom();
                    MG_CHECK(bomber->IsMustDie());
                } else {
                    MG_CHECK(session.remove(3));
                    MG_CHECK(!core->m_Object);
                }
                states.push_back(std::string(detonate ? "detonate:" : "remove:") + session.snapshot());
            }
        }
        if (cleared_at) {
            if (!core->m_Object && !retired_at) retired_at = session.time();
            if (cover->GetHitPoint() != 10 && !damage_at) damage_at = session.time();
            if (!arrived_at && !moving && !returning && escaping->m_PosX == 220 && escaping->m_PosY == 220)
                arrived_at = session.time();
        }
        states.push_back(session.snapshot());
        if (arrived_at && retired_at && session.time() >= arrived_at + 500) break;
    }
    MG_CHECK(cleared_at > 0 && arrived_at > cleared_at && retired_at >= cleared_at);
    MG_CHECK(escaping->m_PosX == 220 && escaping->m_PosY == 220);
    MG_CHECK(!escaping->GetMoveToCoords(destination) && !escaping->GetReturnCoords(previous));
    MG_CHECK(!core->m_Object && !session.matches(3, bomber));
    if (refuge != Refuge::Unreachable) {
        MG_CHECK(safe_at > 0 && maximum_distance > 330);
        MG_CHECK(refuge_at > safe_at && first_return >= refuge_at && first_return < arrived_at);
        MG_CHECK(escaping->GetHitPoint() == 10);
    } else {
        MG_CHECK(safe_at == 0 && first_return == 0 && maximum_distance < 250);
        MG_CHECK(escaping->GetHitPoint() == (detonate ? 9 : 10));
    }
    if (observe_rediversion) MG_CHECK(second_escape > first_return && cleared_at == second_escape);
    else MG_CHECK(second_escape == 0);
    MG_CHECK(cover->GetHitPoint() == (detonate ? 9 : 10));
    if (detonate) MG_CHECK(damage_at == cleared_at + (300 / step + 1) * step);
    else MG_CHECK(damage_at == 0 && retired_at == cleared_at);
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    session.check_running();
    std::cout << "safe=" << safe_at << " refuge=" << refuge_at << " return=" << first_return << " redivert=" << second_escape
        << " cleared=" << cleared_at << " damage=" << damage_at << " retired=" << retired_at
        << " arrival=" << arrived_at << " draws=" << random::draw_count() << '\n';
    return states;
}
void run(Refuge refuge) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) for (bool original : {false, true}) for (bool detonate : {false, true}) {
            const auto reference = journey(refuge, detonate, original, mode, step);
            const auto replay = journey(refuge, detonate, original, mode, step);
            MG_CHECK(reference.size() == replay.size());
            for (size_t index = 0; index < reference.size(); ++index)
                if (reference[index] != replay[index])
                    throw std::runtime_error("Bomb escape divergence at boundary " + std::to_string(index) +
                        "\nExpected: " + reference[index] + "\nActual: " + replay[index]);
        }
}
void free_refuge() { run(Refuge::Free); }
void reserved_refuge() { run(Refuge::Reserved); }
void unreachable_refuge() { run(Refuge::Unreachable); }
}
int main(int argc, char **argv) {
    const tests::Case cases[]{
        {"game.ai.bomb_escape_free_journey", free_refuge},
        {"game.ai.bomb_escape_reserved_journey", reserved_refuge},
        {"game.ai.bomb_escape_unreachable_journey", unreachable_refuge},
    };
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Bomb escape scenario leaked tracked allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
