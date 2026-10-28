// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

namespace tests::capacity {
using battle::Session;

struct GroupImage {
    EMatrixPlayerOrder order;
    CPoint from, to;
    CMatrixMapStatic *target;
    int robots, region, region_count;
    std::array<int, REGION_PATH_MAX_CNT> regions;
    bool war, show, patrol_return;
    CMatrixRoadRoute *route;
    CMatrixCrotch *start, *end;
    int lists, capacity;
    SMatrixRoadRouteHeader *headers;
    SMatrixRoadRouteUnit *units;
    std::vector<std::pair<int, int>> list_data;
    std::vector<std::pair<CMatrixRoad *, CMatrixCrotch *>> route_data;
    bool operator==(const GroupImage &) const = default;
};

inline GroupImage image(const SMatrixPlayerGroup &group) {
    const auto &route = *group.m_RoadPath;
    GroupImage result{group.Order(), group.m_From, group.m_To, group.m_Obj,
        group.m_RobotCnt, group.m_Region, group.m_RegionPathCnt, {},
        group.IsWar(), group.IsShowPlace(), group.IsPatrolReturn(), group.m_RoadPath,
        route.m_Start, route.m_End, route.m_ListCnt, route.m_ListCntMax, route.m_Header, route.m_Units, {}, {}};
    std::copy(std::begin(group.m_RegionPath), std::end(group.m_RegionPath), result.regions.begin());
    for (int list = 0; list < route.m_ListCnt; ++list) {
        const auto &header = route.m_Header[list];
        result.list_data.emplace_back(header.m_Cnt, header.m_Dist);
        for (int unit = 0; unit < header.m_Cnt; ++unit) {
            const auto &value = route.m_Units[list * route.m_Parent->m_CrotchCnt + unit];
            result.route_data.emplace_back(value.m_Road, value.m_Crotch);
        }
    }
    return result;
}

struct WorldImage {
    std::vector<GroupImage> groups;
    // The fixture never dereferences a saved pointer after an action.
    std::vector<std::pair<CMatrixMapStatic *, size_t>> objects;
    std::vector<std::pair<CMatrixRobotAI *, int>> membership;
    std::array<int, MAX_RESOURCES> resources;
    std::vector<CMatrixMapStatic *> selected;
    DWORD flags;
    std::uint64_t draws, icons;
    std::string registered_world;
    bool operator==(const WorldImage &) const = default;
};

struct FullPlayerGroups {
    Session session;
    std::vector<CMatrixRobotAI *> robots;

    explicit FullPlayerGroups(random::Mode mode, int step)
        : session({mode, 327, step, 1000, false, false, true, NavigationLayout::MultiRegion}) {
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE)
                robots.push_back(object->AsRobot());
        }
        MG_CHECK(robots.size() == 2);
        while (robots.size() < MAX_LOGIC_GROUP) {
            const int index = int(robots.size());
            robots.push_back(BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE,
                60 + (index % 8) * 70, 60 + (index / 8) * 70));
        }
        auto *target = CMatrixMapStatic::GetFirstLogic();
        while (target && target->GetSide() == PLAYER_SIDE) target = target->GetNextLogic();
        MG_CHECK(target && target->IsLiveRobot());
        auto *road = g_MatrixMap->m_RN.m_RoadFirst;
        MG_CHECK(road && road->m_Start && road->m_End);
        for (int i = 0; i < MAX_LOGIC_GROUP; ++i) {
            robots[i]->SetGroupLogic(i);
            auto &group = side().m_PlayerGroup[i];
            group.m_RobotCnt = 1;
            group.Order(i % 2 ? mpo_Attack : mpo_Patrol);
            group.m_From = CPoint(8, 8);
            group.m_To = CPoint(8 + 40 * (i % 2), 8 + 40 * ((i / 2) % 2));
            group.m_Obj = target;
            group.m_Region = i % 2;
            group.m_RegionPathCnt = 1;
            group.m_RegionPath[0] = group.m_Region;
            group.SetWar(i % 2 != 0);
            group.SetShowPlace(i % 3 != 0);
            group.SetPatrolReturn(i % 4 != 0);
            auto &route = *group.m_RoadPath;
            route.m_Start = road->m_Start;
            route.m_End = road->m_End;
            const int list = route.AddList();
            route.AddUnit(list, road, road->m_Start);
            route.m_Header[list].m_Dist = road->m_Dist;
        }
    }

    CMatrixSideUnit &side() const { return *g_MatrixMap->GetPlayerSide(); }

    void select_pair() {
        auto *selection = side().GetCurSelGroup();
        selection->RemoveAll();
        selection->AddObject(robots[0], -4);
        selection->AddObject(robots[1], -4);
        side().SetCurGroup(selection);
        side().m_CurrSel = GROUP_SELECTED;
    }

    WorldImage snapshot() const {
        WorldImage result{};
        for (const auto &group : side().m_PlayerGroup) result.groups.push_back(image(group));
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            result.objects.emplace_back(object, object->IsRobot() ? object->AsRobot()->UID : 0);
            if (object->IsRobot()) result.membership.emplace_back(object->AsRobot(), object->AsRobot()->GetGroupLogic());
        }
        if (auto *group = side().GetCurGroup()) {
            for (auto *member = group->m_FirstObject; member; member = member->m_NextObject)
                result.selected.push_back(member->GetObject());
        }
        for (int i = 0; i < MAX_RESOURCES; ++i)
            result.resources[i] = side().GetResourcesAmount(static_cast<ERes>(i));
        result.flags = g_MatrixMap->m_Flags;
        result.draws = random::draw_count();
        result.icons = session.rejected_icon_targets();
        result.registered_world = session.snapshot();
        return result;
    }

    void release_last() {
        g_MatrixMap->StaticDelete(robots.back());
        robots.pop_back();
    }
};
} // namespace tests::capacity
