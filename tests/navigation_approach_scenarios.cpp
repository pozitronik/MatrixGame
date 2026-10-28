// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_flyer.hpp"
#include "battle_building.hpp"
#include "test_support.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "carrier_recording.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

namespace {
using namespace tests::battle;
constexpr int duration = 32000;
struct ReleaseCore {
    void operator()(SObjectCore *core) const { core->Release(); }
};
using Core = std::unique_ptr<SObjectCore, ReleaseCore>;
struct Observation {
    std::vector<std::string> trace;
    std::vector<std::array<uint32_t, 2>> horizontal;
    std::vector<float> altitude;
    std::vector<uint64_t> draws;
    int release{}, landed{}, retired{}, inside{}, first_inside{}, last_inside{};
    float maximum_z{}, maximum_surface{};
    bool operator==(const Observation &) const = default;
};

Observation approach(random::Mode mode, int step, bool tall, int replay) {
    tests::BattleMaterialProfile materials;
    Profile profile{mode, 81, step, duration / step, false, false};
    profile.layout = tests::NavigationLayout::Open;
    profile.unavailable_icon_targets = true;
    Session session(profile);
    tests::install_battle_transport();
    auto &map = *g_MatrixMap;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsRobot()) continue;
        auto *robot = object->AsRobot();
        int id = 1;
        while (id <= 4 && !session.matches(id, robot)) ++id;
        MG_CHECK(id <= 4);
        robot->m_PosX = 100;
        robot->m_PosY = id <= 2 ? 50 + id * 50 : 400 + id * 50;
        robot->MapPosCalc();
        robot->RChange(MR_Matrix);
        robot->RNeed(MR_Matrix);
        robot->JoinToGroup();
        robot->SetMaxSpeed(0);
        robot->m_GroupSpeed = 0;
    }
    auto *mesh = tests::load_battle_mesh({}, nullptr, false, tall ? 500.0f : 20.0f);
    auto owned = tests::make_battle_building(*mesh, PLAYER_SIDE, BUILDING_TITAN, 480, 330);
    auto *obstacle = owned.get();
    map.AddObject(owned.release(), true);
    D3DXVECTOR3 minimum, maximum;
    MG_CHECK(!obstacle->CalcBounds(minimum, maximum));
    // Graph transforms include float normalization; allow a few ULP at these coordinates.
    const auto close = [](float actual, float expected) { return std::abs(actual - expected) < 0.0001f; };
    MG_CHECK(close(minimum.x, 468) && close(minimum.y, 318) && close(minimum.z, 0));
    MG_CHECK(close(maximum.x, 492) && close(maximum.y, 342) && close(maximum.z, tall ? 500.0f : 20.0f));
    D3DXVECTOR3 roof;
    MG_CHECK(map.Trace(&roof, D3DXVECTOR3(480, 330, 1000), D3DXVECTOR3(480, 330, -10),
                       TRACE_BUILDING) == obstacle);
    // A 1,010-unit float ray can lose several sub-millimetre rounding units.
    MG_CHECK(std::abs(roof.z - maximum.z) < 0.001f);
    std::cout << session.metadata("carrier-approach-obstacle-v1") << "\ntall=" << tall
              << " roof=" << roof.z << " bounds=" << minimum.x << ',' << minimum.y << ',' << minimum.z
              << ':' << maximum.x << ',' << maximum.y << ',' << maximum.z << std::endl;
    const auto destination = map.m_RN.m_Place[3].m_Pos;
    tests::carrier::Recording recording(session, mode, step, "approach", int(tall), replay);
    const auto request_draws = random::draw_count();
    map.GetPlayerSide()->OrderFlyer(D3DXVECTOR2(480, 480), FO_GIVE_BOT, 0, 3, destination, 0);
    recording.request(request_draws, 0, {480, 480}, 3, {destination.x, destination.y});
    MG_CHECK(map.m_AD_Obj_cnt == 1);
    auto *carrier = static_cast<CMatrixFlyer *>(map.m_AD_Obj[0]);
    auto *cargo = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
    MG_CHECK(carrier->CarryingRobot() && cargo->m_CurrState == ROBOT_CARRYING);
    Core carrier_core(carrier->GetCore(DEBUG_CALL_INFO));
    Core cargo_core(cargo->GetCore(DEBUG_CALL_INFO));
    const float cargo_health = cargo->GetHitPoint();
    MG_CHECK(cargo_health > 0 && cargo->GetEnv()->m_Place == 3);
    const int group = cargo->GetGroupLogic();
    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
    MG_CHECK(map.GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_Attack);
    MG_CHECK(map.GetPlayerSide()->m_PlayerGroup[group].m_To == destination);
    Observation result;
    const auto sample = [&](std::optional<std::array<float, 3>> position = {}, std::optional<float> height = {},
                            std::optional<float> surface = {}, std::optional<float> hit_roof = {},
                            const std::string &original = {}) {
        const tests::carrier::Pair pair{carrier, cargo, carrier_core.get(), cargo_core.get(), 3,
            {destination.x, destination.y}, position, height,
            tests::carrier::Progress{result.release, result.landed, result.retired, -1}};
        tests::carrier::Frame frame;
        frame.obstacle = obstacle;
        frame.original = original;
        frame.approach = tests::carrier::Approach{{minimum.x, minimum.y, minimum.z},
            {maximum.x, maximum.y, maximum.z}, roof.z, result.maximum_z, result.maximum_surface,
            result.inside, result.first_inside, result.last_inside, surface, hit_roof};
        const auto observation = tests::carrier::observe(session, std::span(&pair, 1), frame);
        recording.snapshot(observation);
    };
    sample();
    while (session.time() < duration) {
        session.advance();
        MG_CHECK(!map.IsPaused());
        MG_CHECK(cargo_core->m_Object == cargo && cargo->IsLiveRobot());
        MG_CHECK(cargo->GetHitPoint() == cargo_health);
        MG_CHECK(std::isfinite(cargo->m_PosX) && std::isfinite(cargo->m_PosY) &&
                 std::isfinite(cargo->GetMatrix()._43));
        std::ostringstream snapshot;
        std::optional<std::array<float, 3>> observed_position;
        std::optional<float> observed_surface, observed_hit_roof;
        snapshot << session.snapshot() << ":carrier=" << bool(carrier_core->m_Object);
        if (carrier_core->m_Object) {
            MG_CHECK(carrier_core->m_Object == carrier);
            MG_CHECK(carrier->GetHitPoint() == 1000);
            const auto position = carrier->GetPos();
            observed_position = std::array<float, 3>{position.x, position.y, position.z};
            MG_CHECK(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
            result.horizontal.push_back({std::bit_cast<uint32_t>(position.x), std::bit_cast<uint32_t>(position.y)});
            result.altitude.push_back(position.z);
            result.maximum_z = std::max(result.maximum_z, position.z);
            const float surface = map.GetZInterpolatedObjRobots(position.x, position.y);
            observed_surface = surface;
            result.maximum_surface = std::max(result.maximum_surface, surface);
            if (position.x > minimum.x && position.x < maximum.x && position.y > minimum.y &&
                position.y < maximum.y && position.z > minimum.z && position.z < maximum.z) {
                ++result.inside;
                if (!result.first_inside) result.first_inside = session.time();
                result.last_inside = session.time();
                MG_CHECK(carrier->CarryingRobot());
                D3DXVECTOR3 hit;
                MG_CHECK(map.Trace(&hit, D3DXVECTOR3(position.x, position.y, 1000),
                                   D3DXVECTOR3(position.x, position.y, -10), TRACE_BUILDING) == obstacle);
                MG_CHECK(hit.z > position.z);
                observed_hit_roof = hit.z;
            }
            snapshot << ',' << std::bit_cast<uint32_t>(position.x) << ',' << std::bit_cast<uint32_t>(position.y)
                     << ',' << std::bit_cast<uint32_t>(position.z);
        }
        else if (!result.retired) result.retired = session.time();
        if (cargo->m_CurrState == ROBOT_CARRYING) {
            MG_CHECK(carrier_core->m_Object && carrier->GetCarryingRobot() == cargo);
        }
        else {
            MG_CHECK(!carrier_core->m_Object || !carrier->CarryingRobot());
        }
        const float observed_height = cargo->GetMatrix()._43;
        snapshot << ":cargo=" << int(cargo->m_CurrState) << ',' << std::bit_cast<uint32_t>(cargo->m_PosX)
                 << ',' << std::bit_cast<uint32_t>(cargo->m_PosY) << ','
                 << std::bit_cast<uint32_t>(observed_height);
        result.trace.push_back(snapshot.str());
        result.draws.push_back(random::draw_count());
        if (!result.release && cargo->m_CurrState == ROBOT_FALLING) result.release = session.time();
        if (!result.landed && cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) result.landed = session.time();
        for (int id = 1; id <= 4; ++id) {
            const auto sentinel = session.robot(id);
            MG_CHECK(sentinel.alive && sentinel.x == 100);
            MG_CHECK(sentinel.y == (id <= 2 ? 50 + id * 50 : 400 + id * 50));
        }
        MG_CHECK(cargo->GetGroupLogic() == group);
        MG_CHECK(map.GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_Attack);
        MG_CHECK(map.GetPlayerSide()->m_PlayerGroup[group].m_To == destination);
        if (session.time() >= 30000) {
            MG_CHECK(cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && cargo->PLIsInPlace());
            MG_CHECK(cargo->m_PosX == 500 && cargo->m_PosY == 500 && cargo->GetMatrix()._43 == 0);
            MG_CHECK(cargo->GetOrdersInPool() == 0);
        }
        sample(observed_position, observed_height, observed_surface, observed_hit_roof, result.trace.back());
    }
    std::cout << "tall=" << tall << " release=" << result.release << " landed=" << result.landed
              << " retired=" << result.retired << " inside=" << result.inside
              << " inside_interval=" << result.first_inside << ',' << result.last_inside
              << " maximum_z=" << result.maximum_z << " surface=" << result.maximum_surface
              << " cargo=" << cargo->m_PosX << ',' << cargo->m_PosY << ',' << cargo->GetMatrix()._43
              << " in_place=" << cargo->PLIsInPlace() << std::endl;
    MG_CHECK(result.release > 0 && result.landed > result.release && result.retired > result.landed);
    MG_CHECK(result.retired < 30000 && !carrier_core->m_Object && map.m_AD_Obj_cnt == 0);
    MG_CHECK(session.rejected_icon_targets() == 1);
    MG_CHECK(map.GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(map.GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    if (tall) {
        MG_CHECK(result.inside >= 10 && result.first_inside > 0);
        MG_CHECK(result.last_inside - result.first_inside >= 200 && result.last_inside < result.release);
    }
    recording.finish();
    return result;
}

template<random::Mode Mode, int Step>
void approach_pair() {
    tests::carrier::invalidate("approach", Mode, Step);
    const auto low = approach(Mode, Step, false, 0);
    MG_CHECK(low == approach(Mode, Step, false, 1));
    const auto high = approach(Mode, Step, true, 0);
    MG_CHECK(high == approach(Mode, Step, true, 1));
    MG_CHECK(low.horizontal == high.horizontal);
    MG_CHECK(low.draws == high.draws);
    MG_CHECK(low.release == high.release && low.landed == high.landed && low.retired == high.retired);
    MG_CHECK(low.inside == 0 && high.inside > 0);
    MG_CHECK(high.maximum_surface > low.maximum_surface + 200);
    bool height_response = false;
    for (size_t i = 0; i < low.altitude.size(); ++i) {
        MG_CHECK(high.altitude[i] >= low.altitude[i]);
        const float x = std::bit_cast<float>(low.horizontal[i][0]);
        const float y = std::bit_cast<float>(low.horizontal[i][1]);
        if (x > 468 && x < 492 && y > 318 && y < 342 && high.altitude[i] > low.altitude[i] + 1)
            height_response = true;
    }
    MG_CHECK(height_response);
    MG_CHECK(low.trace != high.trace);
}
constexpr tests::Case cases[]{
    {"game.navigation.carrier_approach_obstacle.park_miller10", approach_pair<random::Mode::ParkMiller, 10>},
    {"game.navigation.carrier_approach_obstacle.park_miller16", approach_pair<random::Mode::ParkMiller, 16>},
    {"game.navigation.carrier_approach_obstacle.legacy10", approach_pair<random::Mode::LegacyCRT, 10>},
    {"game.navigation.carrier_approach_obstacle.legacy16", approach_pair<random::Mode::LegacyCRT, 16>},
};
}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    if (result == EXIT_SUCCESS) {
        try {
            tests::carrier::complete_after_cleanup();
        }
        catch (const std::exception &error) {
            std::cerr << "Failed to complete carrier recordings: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    return result;
}
