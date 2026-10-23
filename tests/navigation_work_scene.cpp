// MatrixGame - licensed under GPLv2 or any later version.
#include "navigation_work_scene.hpp"
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <sstream>

namespace tests::navigation_work {
namespace {
constexpr int horizon = 32000;
void add(NavigationWork::Counters &to, const NavigationWork::Counters &from) {
    for (auto field : fields) to.*field += from.*field;
}
void position(CMatrixRobotAI &robot, float x, float y) {
    robot.m_PosX = x; robot.m_PosY = y;
    robot.MapPosCalc(); robot.RChange(MR_Matrix); robot.RNeed(MR_Matrix);
    robot.m_CalcBoundsLastTime = g_MatrixMap->GetTime() - 100;
    robot.JoinToGroup();
    MG_CHECK(std::abs(robot.GetGeoCenter().x - x) < .001f && std::abs(robot.GetGeoCenter().y - y) < .001f);
}
}

const char *name(Kind kind) {
    return kind == Kind::CrowdedDestination ? "crowded-destination-v1" : "opposing-flow-v1";
}

struct Scene::Impl {
    Options options;
    battle::Session session;
    // Session IDs 1-4, then setup-created actors 5-10 in construction order.
    std::vector<CMatrixRobotAI *> actors;
    std::array<CMatrixRobotAI *, 8> movers{};
    std::array<D3DXVECTOR2, 8> starts{};
    std::array<CPoint, 8> intentions{};
    std::array<CPoint, 8> assigned{};

    explicit Impl(Options value) : options(value), session(profile(value)) {
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsRobot()) actors.push_back(object->AsRobot());
        MG_CHECK(actors.size() == 4);
        for (size_t i = 0; i < 4; ++i) MG_CHECK(session.matches(static_cast<unsigned>(i + 1), actors[i]));
        for (int i = 0; i < 6; ++i)
            actors.push_back(BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 100));
        movers[0] = actors[0]; movers[1] = actors[1];
        for (int i = 2; i < 8; ++i) movers[i] = actors[i + 2];
        position(*actors[2], 580, 580); position(*actors[3], 540, 540);
        for (auto *enemy : {actors[2], actors[3]}) {
            enemy->SetMaxSpeed(0); enemy->m_GroupSpeed = 0;
        }
        for (int i = 0; i < 8; ++i) {
            const bool opposing = options.kind == Kind::OpposingFlow;
            starts[i] = D3DXVECTOR2(opposing && i >= 4 ? 540.0f : 100.0f,
                                   opposing ? 200.0f + (i % 4) * 60 : 80.0f + i * 60);
            position(*movers[i], starts[i].x, starts[i].y);
            movers[i]->SetGroupLogic(-1);
        }
        auto &side = *g_MatrixMap->GetPlayerSide();
        if (options.kind == Kind::CrowdedDestination) {
            auto *selection = side.GetCurSelGroup();
            selection->RemoveAll();
            for (auto *robot : movers) selection->AddObject(robot, -4);
            side.SetCurGroup(selection); side.m_CurrSel = GROUP_SELECTED;
            session.ground_command(500, 300);
        } else {
            for (int i = 0; i < 8; ++i)
                side.PGOrderMoveTo(side.RobotToLogicGroup(movers[i]), CPoint(i < 4 ? 48 : 8, 18 + (i % 4) * 6));
        }
        for (int i = 0; i < 8; ++i) {
            const auto &group = side.m_PlayerGroup[movers[i]->GetGroupLogic()];
            MG_CHECK(group.Order() == mpo_MoveTo && !group.IsWar());
            intentions[i] = group.m_To;
            MG_CHECK(movers[i]->GetMoveToCoords(assigned[i]));
            for (int j = 0; j < i; ++j) MG_CHECK(assigned[i] != assigned[j]);
        }
        MG_CHECK(NavigationWork::active() == nullptr);
    }

    static battle::Profile profile(Options options) {
        battle::Profile profile{options.mode, 81, options.step_ms, horizon / options.step_ms, false, true};
        profile.layout = options.kind == Kind::OpposingFlow ? NavigationLayout::Open
                                                         : NavigationLayout::CentralObstacle;
        return profile;
    }

    int id(const CMatrixMapStatic *object) const {
        if (!object) return 0;
        const auto found = std::find(actors.begin(), actors.end(), object);
        MG_CHECK(found != actors.end());
        return static_cast<int>(found - actors.begin() + 1);
    }

    std::string snapshot() const {
        const auto draws = random::draw_count();
        std::ostringstream out;
        out << session.snapshot() << "\n{\"actors\":[";
        for (size_t n = 0; n < actors.size(); ++n) {
            auto *robot = actors[n];
            if (n) out << ',';
            out << '[' << n + 1 << ',' << std::bit_cast<std::uint32_t>(robot->m_PosX) << ','
                << std::bit_cast<std::uint32_t>(robot->m_PosY) << ','
                << std::bit_cast<std::uint32_t>(robot->m_Forward.x) << ','
                << std::bit_cast<std::uint32_t>(robot->m_Forward.y) << ','
                << std::bit_cast<std::uint32_t>(robot->GetHitPoint()) << ','
                << robot->GetGroupLogic() << ',' << robot->PLIsInPlace() << ",[";
            for (int i = 0; i < robot->GetOrdersInPool(); ++i) {
                if (i) out << ',';
                const auto &order = *robot->GetOrder(i);
                out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param1) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param2) << ','
                    << std::bit_cast<std::uint32_t>(order.m_Param3) << ',' << order.m_Param4 << ','
                    << id(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
            }
            out << "]]";
        }
        out << "]}";
        MG_CHECK(random::draw_count() == draws);
        return out.str();
    }
};

Scene::Scene(Options options) : impl_(std::make_unique<Impl>(options)) {}
Scene::~Scene() = default;
std::string Scene::metadata() const { return impl_->session.metadata(name(impl_->options.kind)); }

Report Scene::run(bool collect, bool record) {
    auto &s = *impl_;
    MG_CHECK(s.session.frame() == 0 && NavigationWork::active() == nullptr);
    Report result;
    result.events.initial_rng_draws = random::draw_count();
    float minimum = 1000;
    if (record) {
        result.states.reserve(horizon / s.options.step_ms + 1);
        result.frames.reserve(horizon / s.options.step_ms);
        result.states.push_back(s.snapshot());
    }
    while (s.session.time() < horizon) {
        NavigationWork::Counters frame{};
        if (collect) {
            NavigationWork::Capture capture(frame);
            s.session.advance();
        } else s.session.advance();
        MG_CHECK(NavigationWork::active() == nullptr && !g_MatrixMap->IsPaused());
        add(result.total, frame);
        add(result.intervals[s.session.time() <= 4000 ? 0 : s.session.time() <= 12000 ? 1 : 2], frame);
        for (int i = 0; i < 8; ++i) {
            auto &robot = *s.movers[i];
            MG_CHECK(robot.IsLiveRobot() && std::isfinite(robot.m_PosX) && std::isfinite(robot.m_PosY));
            MG_CHECK(robot.m_PosX >= 0 && robot.m_PosX < 640 && robot.m_PosY >= 0 && robot.m_PosY < 640);
            const auto &group = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[robot.GetGroupLogic()];
            MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == s.intentions[i] && !group.IsWar());
            if (!result.events.first_movement[i] &&
                (robot.m_PosX != s.starts[i].x || robot.m_PosY != s.starts[i].y))
                result.events.first_movement[i] = s.session.time();
            if (!result.events.first_arrival[i] && robot.PLIsInPlace())
                result.events.first_arrival[i] = s.session.time();
            for (int j = 0; j < i; ++j)
                minimum = std::min(minimum, std::hypot(robot.m_PosX - s.movers[j]->m_PosX,
                                                      robot.m_PosY - s.movers[j]->m_PosY));
        }
        MG_CHECK(s.actors[2]->m_PosX == 580 && s.actors[2]->m_PosY == 580);
        MG_CHECK(s.actors[3]->m_PosX == 540 && s.actors[3]->m_PosY == 540);
        if (record) {
            result.frames.push_back(frame);
            result.states.push_back(s.snapshot());
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (!result.events.first_arrival[i]) {
            CPoint move(-1, -1);
            s.movers[i]->GetMoveToCoords(move);
            std::cout << "nonarrival actor=" << s.id(s.movers[i]) << " position=" << s.movers[i]->m_PosX << ','
                << s.movers[i]->m_PosY << " move=" << move.x << ',' << move.y
                << " first_movement=" << result.events.first_movement[i] << std::endl;
        }
        MG_CHECK(result.events.first_movement[i] > 0 && result.events.first_arrival[i] > 0);
        MG_CHECK(s.movers[i]->PLIsInPlace() && !s.movers[i]->FindOrderLikeThat(ROT_MOVE_TO));
        if (s.options.kind == Kind::OpposingFlow)
            MG_CHECK(i < 4 ? s.movers[i]->m_PosX > 400 : s.movers[i]->m_PosX < 200);
        result.events.assigned_cells[2 * i] = s.assigned[i].x;
        result.events.assigned_cells[2 * i + 1] = s.assigned[i].y;
        result.events.final_position_bits[2 * i] = std::bit_cast<std::uint32_t>(s.movers[i]->m_PosX);
        result.events.final_position_bits[2 * i + 1] = std::bit_cast<std::uint32_t>(s.movers[i]->m_PosY);
    }
    MG_CHECK(minimum > 0);
    result.events.minimum_separation_bits = std::bit_cast<std::uint32_t>(minimum);
    result.events.final_rng_draws = random::draw_count();
    result.events.frames = s.session.frame(); result.events.time_ms = s.session.time();
    s.session.check_running();
    return result;
}

Report run(Options options, bool collect, bool record) {
    Scene scene(options);
    return scene.run(collect, record);
}

void print(const Report &report) {
    std::cout << "{\"frames\":" << report.events.frames << ",\"time_ms\":" << report.events.time_ms
              << ",\"rng_draws\":" << report.events.final_rng_draws - report.events.initial_rng_draws
              << ",\"minimum_separation\":" << std::bit_cast<float>(report.events.minimum_separation_bits)
              << ",\"counts\":{";
    for (size_t i = 0; i < fields.size(); ++i)
        std::cout << (i ? "," : "") << '"' << names[i] << "\":" << report.total.*fields[i];
    std::cout << "},\"arrivals_ms\":[";
    for (size_t i = 0; i < 8; ++i) std::cout << (i ? "," : "") << report.events.first_arrival[i];
    std::cout << "]}" << std::endl;
}
} // namespace tests::navigation_work
