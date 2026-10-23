// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
using namespace tests;
using namespace tests::battle;

template<bool Cluster> struct MovingFormation {
    static constexpr size_t count = Cluster ? 4 : 3;
    std::array<CMatrixRobotAI *, count> units{};
    std::array<unsigned, count> identities{};
    std::array<CPoint, count> assigned{};
    std::array<bool, count> assigned_known{};
    int group;
    CPoint target;
    int reference_samples = 0, physical_samples = 0;
    bool previous_reference = false;
    float previous_expected = 0;
    float minimum_separation = 10000;

    explicit MovingFormation(Session &session) {
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *selected = side.GetCurSelGroup();
        MG_CHECK(selected && selected->GetObjectsCnt() == 2);
        // Setup only: a stretched line with a designated intermediate member.
        const std::array<float, count> x = [] {
            if constexpr (Cluster) return std::array{80.f, 140.f, 350.f, 500.f};
            else return std::array{80.f, 350.f, 500.f};
        }();
        for (size_t i = 0; i < count; ++i) {
            if (i < 2) {
                units[i] = selected->GetObjectByN(static_cast<int>(i))->AsRobot();
                units[i]->m_PosX = x[i];
                units[i]->m_PosY = 260;
                units[i]->MapPosCalc();
                units[i]->RChange(MR_Matrix);
                units[i]->RNeed(MR_Matrix);
                units[i]->m_CalcBoundsLastTime = g_MatrixMap->GetTime() - 100;
                units[i]->JoinToGroup();
            } else {
                units[i] = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, x[i], 260);
                selected->AddObject(units[i], -4);
            }
            identities[i] = units[i]->UID;
            MG_CHECK(units[i]->GetMaxSpeed() == .6f);
            MG_CHECK(std::abs(units[i]->GetGeoCenter().x - x[i]) < .001f);
            MG_CHECK(std::abs(units[i]->GetGeoCenter().y - 260) < .001f);
        }
        session.ground_command(600, 260);
        group = units[0]->GetGroupLogic();
        MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
        target = side.m_PlayerGroup[group].m_To;
        MG_CHECK(target == CPoint(58, 24));
    }
    bool arrived() const {
        return std::all_of(units.begin(), units.end(), [](const auto *r) { return r->PLIsInPlace(); });
    }
    void tick(Session &session) {
        std::array<float, count> previous_x{}, previous_y{}, previous_speed{};
        for (size_t i = 0; i < count; ++i) {
            previous_x[i] = units[i]->m_PosX;
            previous_y[i] = units[i]->m_PosY;
            previous_speed[i] = units[i]->m_GroupSpeed;
        }
        const int previous_time = session.time();
        session.advance();
        const auto &intent = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group];
        MG_CHECK(intent.Order() == mpo_MoveTo && intent.m_To == target && intent.m_RobotCnt == count);
        std::array<CPoint, count> moves{};
        bool all_moving = true;
        float cx = 0, cy = 0, tx = 0, ty = 0;
        for (size_t i = 0; i < count; ++i) {
            auto &r = *units[i];
            bool live = false;
            for (auto *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic())
                if (o == &r) live = o->IsLiveRobot() && r.UID == identities[i];
            MG_CHECK(live && r.GetGroupLogic() == group);
            MG_CHECK(r.GetMaxSpeed() == .6f);
            MG_CHECK(std::isfinite(r.m_PosX) && std::isfinite(r.m_PosY) && std::isfinite(r.m_Speed));
            MG_CHECK(r.m_Speed >= 0 && r.m_Speed <= .6f + .000001f);
            MG_CHECK(std::isfinite(r.m_GroupSpeed) && r.m_GroupSpeed >= .36f - .000001f && r.m_GroupSpeed <= .6f);
            MG_CHECK(r.m_PosX >= 20 && r.m_PosX <= 620 && r.m_PosY >= 20 && r.m_PosY <= 620);
            CPoint returned;
            const bool moving = r.GetMoveToCoords(moves[i]);
            if (moving) {
                if (assigned_known[i]) MG_CHECK(moves[i] == assigned[i]);
                else {
                    assigned[i] = moves[i];
                    assigned_known[i] = true;
                }
            }
            all_moving &= moving && !r.GetReturnCoords(returned) && r.GetEnv()->GetEnemyCnt() == 0;
            cx += r.m_PosX;
            cy += r.m_PosY;
            tx += moving ? 10.f * moves[i].x + 20 : r.m_PosX;
            ty += moving ? 10.f * moves[i].y + 20 : r.m_PosY;
            for (size_t j = 0; j < i; ++j) {
                const float gap = std::hypot(r.m_PosX - units[j]->m_PosX, r.m_PosY - units[j]->m_PosY);
                MG_CHECK(gap >= 2 * COLLIDE_BOT_R - .001f);
                minimum_separation = std::min(minimum_separation, gap);
            }
        }
        // Objects consume the previous frame's allowance before side regulation.
        constexpr size_t middle = count - 2, leader = count - 1, reference = Cluster ? 1 : 0;
        if (previous_reference) {
            const float travelled = std::hypot(units[middle]->m_PosX - previous_x[middle],
                                               units[middle]->m_PosY - previous_y[middle]);
            const float expected = previous_expected * (session.time() - previous_time) / LOGIC_TAKT_PERIOD;
            if (travelled > .01f && std::abs(units[middle]->m_Speed - previous_speed[middle]) < .000001f) {
                MG_CHECK(std::abs(travelled - expected) < .001f);
                ++physical_samples;
                if (physical_samples <= 3)
                    std::cout << "physical time=" << session.time() << " dt=" << session.time() - previous_time
                              << " preceding_allowance=" << previous_speed[middle]
                              << " expected_allowance=" << previous_expected << " travelled=" << travelled
                              << " expected_distance=" << expected << std::endl;
            }
        }
        previous_reference = false;
        // A named geometry window establishes the intended reference independently
        // of production's sort/scan. No reference-selection loop is duplicated here.
        const float dx = (tx - cx) / count, dy = (ty - cy) / count;
        const float length = std::hypot(dx, dy);
        if (!all_moving || length < 100) return;
        std::array<float, count> projection{};
        for (size_t i = 0; i < count; ++i)
            projection[i] = (dx * units[i]->m_PosX + dy * units[i]->m_PosY) / length;
        const float spread = projection[leader] - projection[reference];
        const float middle_ahead = projection[middle] - projection[reference];
        const float front_gap = projection[leader] - projection[middle];
        bool window = middle_ahead > 190 && front_gap > 130 && spread > 330;
        if constexpr (Cluster) {
            const float rear_gap = projection[1] - projection[0];
            window &= rear_gap > 40 && rear_gap < 100;
        }
        if (!window) return;
        const float expected = .6f * (.6f + .4f * front_gap / (spread - 180));
        MG_CHECK(expected > .37f && expected < .59f);
        MG_CHECK(std::abs(units[middle]->m_GroupSpeed - expected) < .000001f);
        MG_CHECK(std::abs(units[leader]->m_GroupSpeed - .36f) < .000001f);
        MG_CHECK(units[reference]->m_GroupSpeed == .6f);
        if constexpr (Cluster) {
            const float absolute_spread = projection[leader] - projection[0];
            const float absolute_expected = .6f * (.6f + .4f * front_gap / (absolute_spread - 180));
            MG_CHECK(expected - absolute_expected > .01f);
            // Selecting the later gap makes the middle member the reference,
            // leaving it at .6 rather than this observed intermediate allowance.
            MG_CHECK(.6f - expected > .01f);
        }
        ++reference_samples;
        previous_reference = true;
        previous_expected = expected;
        if (reference_samples <= 3)
            std::cout << "time=" << session.time() << " reference=" << projection[reference]
                      << " rear=" << projection[0] << " middle=" << projection[middle]
                      << " leader=" << projection[leader] << " expected=" << expected
                      << " actual=" << units[middle]->m_GroupSpeed << std::endl;
    }
};

template<bool Cluster, random::Mode Mode, int Step> void journey() {
    Profile profile{Mode, 81, Step, 6000, false, true};
    profile.layout = NavigationLayout::Open;
    Session session(profile);
    MovingFormation<Cluster> formation(session);
    const auto initial_draws = random::draw_count();
    std::cout << session.metadata(Cluster ? "formation-moving-reference-v1" : "formation-moving-intermediate-v1")
              << std::endl;
    while (session.time() < 50000 && !formation.arrived()) formation.tick(session);
    std::cout << "arrival=" << session.time() << " arrived=" << formation.arrived()
              << " reference_samples=" << formation.reference_samples
              << " physical_samples=" << formation.physical_samples
              << " minimum_separation=" << formation.minimum_separation << std::endl;
    MG_CHECK(formation.reference_samples >= 10 && formation.physical_samples >= 10);
    MG_CHECK(formation.arrived());
    std::array<D3DXVECTOR2, MovingFormation<Cluster>::count> final{};
    const auto expected_places = [] {
        if constexpr (Cluster) return std::array{CPoint(58, 24), CPoint(54, 20), CPoint(54, 28), CPoint(58, 20)};
        else return std::array{CPoint(58, 24), CPoint(54, 20), CPoint(54, 28)};
    }();
    for (size_t i = 0; i < final.size(); ++i) {
        MG_CHECK(formation.assigned_known[i]);
        MG_CHECK(formation.assigned[i] == expected_places[i]);
        final[i] = {10.f * formation.assigned[i].x + 20, 10.f * formation.assigned[i].y + 20};
        MG_CHECK(formation.units[i]->m_PosX == final[i].x && formation.units[i]->m_PosY == final[i].y);
        for (size_t j = 0; j < i; ++j) MG_CHECK(formation.assigned[i] != formation.assigned[j]);
        std::cout << "unit=" << i << " assigned=" << formation.assigned[i].x << ',' << formation.assigned[i].y
                  << " final=" << final[i].x << ',' << final[i].y << std::endl;
    }
    for (int frame = 0; frame < 20; ++frame) {
        formation.tick(session);
        for (size_t i = 0; i < final.size(); ++i) {
            MG_CHECK(formation.units[i]->PLIsInPlace());
            MG_CHECK(formation.units[i]->m_PosX == final[i].x && formation.units[i]->m_PosY == final[i].y);
        }
    }
    for (auto *r : formation.units) MG_CHECK(r->GetOrdersInPool() == 0);
    std::cout << "observed_rng_draws=" << random::draw_count() - initial_draws << std::endl;
}

#define PROFILE(Name, Mode, Step) \
    {"game.navigation.formation_movement.intermediate." Name, journey<false, Mode, Step>}, \
    {"game.navigation.formation_movement.trailing_reference." Name, journey<true, Mode, Step>}
const tests::Case cases[]{
    PROFILE("park_miller10", random::Mode::ParkMiller, 10),
    PROFILE("park_miller16", random::Mode::ParkMiller, 16),
    PROFILE("legacy10", random::Mode::LegacyCRT, 10),
    PROFILE("legacy16", random::Mode::LegacyCRT, 16),
};
#undef PROFILE
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
