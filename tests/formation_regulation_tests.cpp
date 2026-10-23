// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
using namespace tests;
using namespace tests::battle;
struct TrackAccess : CMatrixEffect {
    static SSpotProperties &properties() { return m_SpotProperties[SPOT_SOLE_TRACK]; }
    static SSpotProperties wheel() { return m_SpotProperties[SPOT_SOLE_WHEEL]; }
};
struct TrackEffects {
    SSpotProperties previous = TrackAccess::properties();
    TrackEffects() { TrackAccess::properties() = TrackAccess::wheel(); }
    ~TrackEffects() { TrackAccess::properties() = previous; }
};
struct Formation {
    std::array<CMatrixRobotAI *, 3> units;
    std::array<unsigned, 3> identities;
    int group = -1;
    CPoint target;
    std::array<int, 3> slowed{}, full{}, physical_slow{}, physical_full{};
    float minimum_separation = 10000, maximum_separation = 0;
    explicit Formation(CMatrixRobotAI *track) {
        auto *selected = g_MatrixMap->GetPlayerSide()->GetCurGroup();
        MG_CHECK(selected && selected->GetObjectsCnt() == 3);
        for (int i = 0; i < 3; ++i) MG_CHECK(selected->GetObjectByN(i) && selected->GetObjectByN(i)->IsLiveRobot());
        MG_CHECK(selected->GetObjectByN(2) == track);
        units = {selected->GetObjectByN(0)->AsRobot(), selected->GetObjectByN(1)->AsRobot(), track};
        for (int i = 0; i < 3; ++i) identities[i] = units[i]->UID;
    }
    void command(Session &session, float x, float y) {
        session.ground_command(x, y);
        group = units[0]->GetGroupLogic();
        MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
        target = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].m_To;
    }
    bool arrived() const {
        return std::all_of(units.begin(), units.end(), [](const auto *unit) { return unit->PLIsInPlace(); });
    }
    float separation() const {
        float value = 0;
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
            value = std::max(value, std::hypot(units[i]->m_PosX - units[j]->m_PosX, units[i]->m_PosY - units[j]->m_PosY));
        return value;
    }
    void tick(Session &session) {
        const int previous_time = session.time();
        std::array<float, 3> previous_speed, previous_x, previous_y;
        for (int i = 0; i < 3; ++i) {
            previous_speed[i] = units[i]->m_GroupSpeed;
            previous_x[i] = units[i]->m_PosX;
            previous_y[i] = units[i]->m_PosY;
        }
        session.advance();
        auto &intent = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group];
        MG_CHECK(intent.Order() == mpo_MoveTo && intent.m_To == target && intent.m_Obj == nullptr && intent.m_RobotCnt == 3);
        for (int i = 0; i < 3; ++i) {
            bool found = false;
            for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                if (object == units[i]) found = object->IsLiveRobot() && object->AsRobot()->UID == identities[i];
            MG_CHECK(found);
            auto &unit = *units[i];
            MG_CHECK(unit.GetGroupLogic() == group);
            MG_CHECK(std::isfinite(unit.m_PosX) && std::isfinite(unit.m_PosY));
            MG_CHECK(std::isfinite(unit.m_GroupSpeed) && std::isfinite(unit.m_Speed));
            MG_CHECK(unit.m_PosX >= 20 && unit.m_PosX <= 620 && unit.m_PosY >= 20 && unit.m_PosY <= 620);
            MG_CHECK(unit.m_GroupSpeed >= .6f * unit.GetMaxSpeed() - .000001f && unit.m_GroupSpeed <= unit.GetMaxSpeed());
            MG_CHECK(unit.m_Speed >= 0 && unit.m_Speed <= unit.GetMaxSpeed() + .000001f);
            if (unit.m_GroupSpeed < unit.GetMaxSpeed() - .000001f) ++slowed[i];
            else ++full[i];
            // Objects move before the side recomputes the next frame's allowance.
            const bool progressed = unit.m_PosX != previous_x[i] || unit.m_PosY != previous_y[i];
            if (progressed && std::abs(unit.m_Speed - previous_speed[i]) < .000001f) {
                const float travelled = std::hypot(unit.m_PosX - previous_x[i], unit.m_PosY - previous_y[i]);
                const float expected = previous_speed[i] * (session.time() - previous_time) / LOGIC_TAKT_PERIOD;
                if (previous_speed[i] < unit.GetMaxSpeed() - .000001f) {
                    MG_CHECK(std::abs(travelled - expected) < .001f);
                    ++physical_slow[i];
                } else if (std::abs(travelled - expected) < .001f) {
                    ++physical_full[i];
                }
            }
        }
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) {
            const float gap = std::hypot(units[i]->m_PosX - units[j]->m_PosX,units[i]->m_PosY - units[j]->m_PosY);
            MG_CHECK(gap >= 2 * COLLIDE_BOT_R);
            minimum_separation = std::min(minimum_separation, gap);
            maximum_separation = std::max(maximum_separation, gap);
        }
    }
};
void journey(random::Mode mode, int step) {
    Profile profile{mode, 81, step, 6000, false, true};
    profile.layout = NavigationLayout::Open;
    Session session(profile);
    TrackEffects effects;
    g_Config.m_ItemChars[CHASSIS3_MOVE_SPEED] = .3f;
    g_Config.m_ItemChars[CHASSIS3_ROTATION_SPEED] = .04f;
    g_Config.m_ItemChars[CHASSIS3_STRUCTURE] = 1000;
    g_Config.m_ItemChars[CHASSIS3_MOVE_WATER_CORR] = .5f;
    g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_DOWN] = 2.5f;
    g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_UP] = .7f;
    auto *track = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 320, RUK_CHASSIS_TRACK);
    g_MatrixMap->GetPlayerSide()->GetCurSelGroup()->AddObject(track, -4);
    Formation formation(track);
    MG_CHECK(formation.units[0]->GetMaxSpeed() == .6f && formation.units[1]->GetMaxSpeed() == .6f);
    MG_CHECK(track->GetMaxSpeed() == .3f);
    std::cout << session.metadata("mixed-formation-regulation-v1") << std::endl;
    formation.command(session, 600, 200);
    MG_CHECK(formation.target == CPoint(58, 18));
    int replacement = 0, first_regulated = 0;
    while (session.time() < 20000) {
        formation.tick(session);
        auto &leader = *formation.units[0];
        if (leader.m_GroupSpeed < leader.GetMaxSpeed()) {
            if (!first_regulated) first_regulated = session.time();
            MG_CHECK(leader.m_PosX > formation.units[1]->m_PosX && leader.m_PosX > track->m_PosX);
            MG_CHECK(std::abs(leader.m_GroupSpeed - .36f) < .000001f);
            MG_CHECK(formation.units[1]->m_GroupSpeed == .6f && track->m_GroupSpeed == .3f);
        }
        // Require repeated regulation, rather than replacing at a one-frame threshold crossing.
        if (formation.slowed[0] > 20 && formation.separation() > 180) {
            replacement = session.time();
            break;
        }
    }
    std::cout << session.metadata("mixed-formation-regulation-v1") << " first_regulated=" << first_regulated
              << " replacement=" << replacement << " stretched_separation=" << formation.separation() << std::endl;
    MG_CHECK(first_regulated > 0 && replacement > first_regulated && !formation.arrived());
    MG_CHECK(formation.slowed[0] == 21);
    MG_CHECK(formation.physical_slow[0] > 0 && formation.slowed[1] == 0 && formation.slowed[2] == 0);
    const int former_leader_full_samples = formation.physical_full[0];
    const float stretched = formation.separation();
    MG_CHECK(std::hypot(formation.units[0]->m_PosX - 100, formation.units[0]->m_PosY - 500) >
             std::hypot(track->m_PosX - 100, track->m_PosY - 500));
    formation.command(session, 100, 500);
    MG_CHECK(formation.target == CPoint(8, 48));
    formation.tick(session);
    // The former leader now trails the replacement route and regains its normal allowance.
    for (auto *unit : formation.units) MG_CHECK(unit->m_GroupSpeed == unit->GetMaxSpeed());
    while (session.time() < 50000 && !formation.arrived()) formation.tick(session);
    MG_CHECK(formation.arrived() && formation.physical_full[0] > former_leader_full_samples);
    const int arrival = session.time();
    const std::array<CPoint, 3> expected{CPoint(100, 500), CPoint(60, 460), CPoint(60, 540)};
    auto check_arrival = [&] {
        for (int i = 0; i < 3; ++i) {
            auto &unit = *formation.units[i];
            MG_CHECK(unit.m_PosX == expected[i].x && unit.m_PosY == expected[i].y);
            MG_CHECK(unit.PLIsInPlace());
        }
        MG_CHECK(formation.separation() == 80 && formation.separation() < stretched);
    };
    check_arrival();
    for (int frame = 0; frame < 20; ++frame) {
        formation.tick(session);
        check_arrival();
    }
    for (auto *unit : formation.units) MG_CHECK(unit->GetOrdersInPool() == 0);
    for (int i = 0; i < 3; ++i)
        std::cout << "unit=" << i << " max_speed=" << formation.units[i]->GetMaxSpeed()
                  << " slowed_samples=" << formation.slowed[i] << " physical_slow_samples=" << formation.physical_slow[i]
                  << " full_samples=" << formation.full[i] << " physical_full_samples=" << formation.physical_full[i] << '\n';
    std::cout << "arrival=" << arrival << " minimum_separation=" << formation.minimum_separation
              << " maximum_separation=" << formation.maximum_separation << " final_separation=" << formation.separation() << std::endl;
}
void regulation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) journey(mode, step);
}
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const tests::Case cases[] = {{"game.navigation.mixed_formation_regulation", regulation}};
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
