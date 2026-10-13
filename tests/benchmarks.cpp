// MatrixGame - licensed under GPLv2 or any later version.
// Representative component workloads for performance measurements. Every
// workload drives production engine code with synthetic data; none loads game
// resources, creates a window or graphics device, or initializes audio.
#include "benchmark_support.hpp"
#include "command_world.hpp"
#include "test_support.hpp"

#include "MatrixFormGame.hpp"
#include "MatrixLogic.hpp"
#include "MatrixMultiSelection.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "input.hpp"

#include <random.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Synthetic visibility replaces only mesh/frustum projection, as in the selection regressions.
struct SelectionFixture {
    static void show(CMatrixMapStatic *object) { CMatrixMapStatic::objects[CMatrixMapStatic::objects_rite++] = object; }
    static void hideAll() {
        for (int i = 0; i < CMatrixMapStatic::objects_rite; ++i) CMatrixMapStatic::objects[i] = nullptr;
        CMatrixMapStatic::objects_left = CMatrixMapStatic::objects_rite = 0;
    }
    static void deleteSelections() {
        while (CMultiSelection::m_First) {
            auto *selection = CMultiSelection::m_First;
            HDelete(CMultiSelection, selection, g_MatrixHeap);
        }
        CMultiSelection::m_GameSelection = nullptr;
    }
};

// Robot state access for mesh-free fixtures. Movement itself never runs.
struct RobotCommandFixture {
    static void place(CMatrixRobotAI &robot, CPoint cell) {
        robot.m_MapX = cell.x;
        robot.m_MapY = cell.y;
    }

    // The route step of the ROT_MOVE_TO branch in CMatrixRobotAI::LogicTakt for a
    // robot that has a fresh move order and no move path. LogicTakt as a whole needs
    // robot geometry, so these lines are repeated here; every call is production code.
    static void route(CMatrixRobotAI &robot) {
        robot.ZoneCurFind();
        if (robot.m_ZoneCur < 0) return;
        if (robot.m_ZoneDes < 0) {
            robot.m_ZoneDes = g_MatrixMap->ZoneFindNear(robot.m_Unit[0].u1.s1.m_Kind - 1, robot.m_DesX, robot.m_DesY);
            robot.m_ZonePathCnt = 0;
        }
        if (robot.m_ZonePathCnt <= 0) robot.ZonePathCalc();
        robot.ZoneMoveCalc();
    }

    static int zonePathLength(const CMatrixRobotAI &robot) { return robot.m_ZonePathCnt; }
    static int movePathLength(const CMatrixRobotAI &robot) { return robot.m_MovePathCnt; }
};

namespace {
using tests::EnginePtr;
using tests::make_engine_object;

constexpr unsigned benchmarkSeed = 78;
constexpr int robotSize = ROBOT_MOVECELLS_PER_SIZE;
constexpr int wheelChassis = RUK_CHASSIS_WHEEL - 1;  // Movement-type index used by the engine.

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

// Counts Park-Miller draws since seeding through the public API, then restores the state.
std::int64_t drawsSinceSeed(unsigned seed) {
    require(random::mode() == random::Mode::ParkMiller, "draw counting requires the Park-Miller mode");
    const auto target = static_cast<std::uint32_t>(random::Rnd()) + 1;
    std::minstd_rand0 replay(seed);
    std::uint32_t before = seed % std::minstd_rand0::modulus ? seed % std::minstd_rand0::modulus : 1;
    std::int64_t steps = 0;
    while (true) {
        const auto state = static_cast<std::uint32_t>(replay());
        ++steps;
        if (state == target) break;
        require(steps < 100000000, "random draw count exceeds the inspection limit");
        before = state;
    }
    random::seed(before, random::Mode::ParkMiller);
    return steps - 1;
}

// Exposes map time to drive the production side catch-up loop without
// presentation, interface or device work.
struct ClockedMap : CMatrixMapLogic {
    void advance(int ms) { m_Time += ms; }
};

struct Globals {
    const DWORD flags = g_Flags;
    CIFaceList *const interfaceList = g_IFaceList;
    ~Globals() {
        g_IFaceList = interfaceList;
        g_MatrixMap = nullptr;
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
        g_Flags = flags;
    }
};

// Production map/side ownership with no meshes, packages, window or device.
struct World {
    Globals globals;
    CHeap heap;
    CBlockPar data;
    EnginePtr<ClockedMap> map;
    EnginePtr<CIFaceList> ui;

    World() {
        g_Flags = 0;
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        random::seed(benchmarkSeed, random::Mode::ParkMiller);
        CacheInit();
        try {
            g_Config.SetDefaults();
            Input::onFocusLost();
            map = make_engine_object<ClockedMap>();
            g_MatrixMap = map.get();
            map->m_Side = HNew(g_MatrixHeap) CMatrixSideUnit;
            map->m_SideCnt = 1;
            map->m_PlayerSide = map->m_Side;
            map->m_PlayerSide->m_Id = PLAYER_SIDE;
            ui = make_engine_object<CIFaceList>();
            g_IFaceList = ui.get();
            map->m_PlayerSide->InitPlayerSide();
        }
        catch (...) {
            clear();
            throw;
        }
    }
    ~World() { clear(); }

    void clear() {
        Input::onFocusLost();
        ui.reset();
        g_IFaceList = globals.interfaceList;
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
    }

    CMatrixSideUnit &side() { return *map->GetPlayerSide(); }
};

// A 128x128 movement grid (64x64 map units) split into 8x8-cell zones. Three
// walls with alternating gaps force a serpentine route across the map.
struct NavigationGrid {
    static constexpr int units = 64;
    static constexpr int zoneCells = 8;
    static constexpr int cells = units * MOVE_CNT;
    static constexpr int zonesPerRow = cells / zoneCells;
    int walls = 0;

    static bool wall(int x, int y) {
        struct Wall {
            int x, gapTop, gapBottom;
        };
        static constexpr Wall layout[] = {{30, 100, 112}, {62, 16, 28}, {94, 100, 112}};
        for (const auto &w : layout) {
            if ((x == w.x || x == w.x + 1) && (y < w.gapTop || y >= w.gapBottom)) return true;
        }
        return false;
    }

    static int zoneOf(int x, int y) { return (y / zoneCells) * zonesPerRow + x / zoneCells; }

    explicit NavigationGrid(CMatrixMapLogic &map) {
        map.UnitInit(units, units);
        require(map.m_SizeMove.x == cells && map.m_SizeMove.y == cells, "unexpected movement grid size");
        for (int y = 0; y < cells; ++y) {
            for (int x = 0; x < cells; ++x) {
                auto *cell = map.MoveGet(x, y);
                cell->m_Zone = zoneOf(x, y);
                if (wall(x, y)) ++walls;
                for (int size = 1; size <= 5; ++size) {
                    bool blocked = x + size > cells || y + size > cells;
                    for (int dy = 0; dy < size && !blocked; ++dy) {
                        for (int dx = 0; dx < size && !blocked; ++dx) blocked = wall(x + dx, y + dy);
                    }
                    // Block every chassis type for this footprint size.
                    if (blocked) cell->m_Stop |= DWORD(0x1f) << (6 * (size - 1));
                }
            }
        }

        auto &network = map.m_RN;
        network.SetZoneCntMax(zonesPerRow * zonesPerRow);
        network.m_ZoneCnt = zonesPerRow * zonesPerRow;
        for (int zy = 0; zy < zonesPerRow; ++zy) {
            for (int zx = 0; zx < zonesPerRow; ++zx) {
                auto &zone = network.m_Zone[zy * zonesPerRow + zx];
                const int left = zx * zoneCells;
                const int top = zy * zoneCells;
                zone.m_Rect = CRect(left, top, left + zoneCells, top + zoneCells);
                zone.m_Center = CPoint(left + zoneCells / 2, top + zoneCells / 2);
                zone.m_Size = zoneCells * zoneCells;
                zone.m_Region = -1;
                const CPoint neighbours[] = {{zx - 1, zy}, {zx + 1, zy}, {zx, zy - 1}, {zx, zy + 1}};
                for (const auto &n : neighbours) {
                    if (n.x < 0 || n.y < 0 || n.x >= zonesPerRow || n.y >= zonesPerRow) continue;
                    const int index = zone.m_NearZoneCnt++;
                    zone.m_NearZone[index] = n.y * zonesPerRow + n.x;
                    zone.m_NearZoneConnectSize[index] = zoneCells;
                    zone.m_NearZoneMove[index] = connected(map, zx, zy, n.x, n.y) ? 0 : 0x1f;
                }
            }
        }
    }

    // Two adjacent zones connect when a robot footprint fits on both sides of their border.
    static bool connected(CMatrixMapLogic &map, int ax, int ay, int bx, int by) {
        const DWORD mask = DWORD(1 << wheelChassis) << (6 * (robotSize - 1));
        for (int i = 0; i < zoneCells; ++i) {
            CPoint a, b;
            if (ax != bx) {
                const int border = std::max(ax, bx) * zoneCells;
                a = CPoint(border - 1, ay * zoneCells + i);
                b = CPoint(border, ay * zoneCells + i);
            } else {
                const int border = std::max(ay, by) * zoneCells;
                a = CPoint(ax * zoneCells + i, border - 1);
                b = CPoint(ax * zoneCells + i, border);
            }
            const auto *ca = map.MoveGetTest(a.x, a.y);
            const auto *cb = map.MoveGetTest(b.x, b.y);
            if (ca && cb && !(ca->m_Stop & mask) && !(cb->m_Stop & mask)) return true;
        }
        return false;
    }
};

// Mesh-free player robot. Fixture setup assigns ownership, chassis and group;
// order evaluation and counting use production side code.
struct IdleRobot : CMatrixRobotAI {
    IdleRobot(int group, CPoint cell) {
        m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
        m_Side = PLAYER_SIDE;
        m_Unit[0].u1.s1.m_Kind = RUK_CHASSIS_WHEEL;
        RobotCommandFixture::place(*this, cell);
        m_PosX = GLOBAL_SCALE_MOVE * cell.x;
        m_PosY = GLOBAL_SCALE_MOVE * cell.y;
        SetGroupLogic(group);
        AddLT();
    }
};

// ---------------------------------------------------------------------------
// navigation.crowd_move: one player move order for a crowded group. The side
// assigns places and issues robot move orders through production code; each
// robot then calculates its zone route and local path through its production
// route functions, in logic-list order as the object pass would.

constexpr int crowdRobots = 48;
const CPoint crowdTarget(112, 60);

CPoint crowdStart(int index) { return CPoint(2 + (index % 6) * 4, 8 + (index / 6) * 12); }

class CrowdMoveSession final : public bench::Session {
public:
    CrowdMoveSession() : grid(*world.map) {
        for (int i = 0; i < crowdRobots; ++i) {
            const CPoint start = crowdStart(i);
            require(world.map->IsAbsenceWall(wheelChassis, robotSize, start.x, start.y), "crowd start is blocked");
            robots.emplace_back(HNew(g_MatrixHeap) IdleRobot(0, start));
        }
        // Places may change only after the start-up delay of a robot's place search.
        world.map->advance(3000);
    }

    ~CrowdMoveSession() override { robots.clear(); }

    // A new order starts from stopped robots; the side evaluates groups every 100 ms.
    void prepare() override {
        for (auto &robot : robots) robot->StopMoving();
        world.map->advance(100);
    }

    void run(bench::Counters &work) override { order(work, nullptr); }

    void inspect(bench::Counters &details) override {
        prepare();
        bench::Counters ignored;
        order(ignored, &details);
        details.add("grid_cells", NavigationGrid::cells * NavigationGrid::cells);
        details.add("wall_cells", grid.walls);
        details.add("zones", world.map->m_RN.m_ZoneCnt);
        details.add("rng_draws", drawsSinceSeed(benchmarkSeed));
    }

private:
    void order(bench::Counters &work, bench::Counters *details) {
        auto &side = world.side();
        auto &group = side.m_PlayerGroup[0];
        // CMatrixSideUnit::PGOrderMoveTo without its acknowledgement sound and place
        // markers, which need audio data and a device.
        group.Order(mpo_MoveTo);
        group.m_To = crowdTarget;
        group.m_Obj = nullptr;
        group.m_RoadPath->Clear();
        side.PGRemoveAllPassive(0, group.m_Obj);
        side.PGAssignPlacePlayer(0, crowdTarget);
        side.TaktPL();
        work.add("orders", 1);

        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsLiveRobot()) continue;
            auto &robot = *object->AsRobot();
            CPoint destination;
            if (!robot.GetMoveToCoords(destination)) continue;
            work.add("robots_ordered", 1);
            if (details) clearSearchMarks();
            RobotCommandFixture::route(robot);
            if (details) details->add("local_cells_visited", visitedCells());
            work.add("zone_path_zones", RobotCommandFixture::zonePathLength(robot));
            const int points = RobotCommandFixture::movePathLength(robot);
            work.add("move_path_points", points);
            if (points <= 0) work.add("route_failures", 1);
        }
    }

    // The search initializes the marks inside its own bounds; clearing the rest
    // only makes the count below specific to one robot.
    void clearSearchMarks() {
        for (int y = 0; y < NavigationGrid::cells; ++y) {
            for (int x = 0; x < NavigationGrid::cells; ++x) world.map->MoveGet(x, y)->m_Find = -1;
        }
    }

    // The production search leaves its cost marks in the movement grid.
    std::int64_t visitedCells() {
        std::int64_t visited = 0;
        for (int y = 0; y < NavigationGrid::cells; ++y) {
            for (int x = 0; x < NavigationGrid::cells; ++x) visited += world.map->MoveGet(x, y)->m_Find >= 0 ? 1 : 0;
        }
        return visited;
    }

    World world;
    NavigationGrid grid;
    std::vector<EnginePtr<IdleRobot>> robots;
};

// ---------------------------------------------------------------------------
// selection.drag_refresh: one rectangle drag over many visible objects through
// the real form input and the per-frame pending refresh.

constexpr int visibleObjects = 600;
constexpr int dragFrames = 60;

struct ScreenBuilding : CMatrixBuilding {
    CPoint screen;
    mutable std::int64_t *tests = nullptr;
    bool InRect(const CRect &rect) const override {
        ++*tests;
        return rect.IsInRect(screen);
    }
};

class SelectionDragSession final : public bench::Session {
public:
    SelectionDragSession() {
        SelectionFixture::hideAll();
        for (int i = 0; i < visibleObjects; ++i) {
            auto *object = HNew(g_MatrixHeap) ScreenBuilding;
            object->m_Side = (i % 3 == 2) ? 2 : PLAYER_SIDE;
            object->screen = CPoint(8 + (i % 30) * 32, 8 + (i / 30) * 36);
            object->tests = &rectangleTests;
            objects.push_back(object);
            SelectionFixture::show(object);
        }
        CMultiSelection::StaticInit();
    }

    ~SelectionDragSession() override {
        clearSelection();
        SelectionFixture::hideAll();
        for (auto *object : objects) HDelete(ScreenBuilding, object, g_MatrixHeap);
    }

    void run(bench::Counters &work) override {
        rectangleTests = 0;
        CMultiSelection::m_GameSelection = CMultiSelection::Begin(CPoint(4, 4));
        require(CMultiSelection::m_GameSelection != nullptr, "selection did not start");
        auto *selection = CMultiSelection::m_GameSelection;
        for (int frame = 1; frame <= dragFrames; ++frame) {
            // Several queued moves per frame; only the latest rectangle is evaluated.
            for (int step = 0; step < 4; ++step) form.MouseMove(frame * 15 + step, frame * 10 + step);
            if (selection->UpdatePending(TRACE_ROBOT | TRACE_BUILDING, nullptr, 0)) work.add("refreshes", 1);
        }
        form.MouseKey(B_UP, VK_LBUTTON, dragFrames * 15, dragFrames * 10);
        work.add("frames", dragFrames);
        work.add("rectangle_tests", rectangleTests);
        // Releasing over buildings only selects the first one as the active object.
        work.add("building_selected", world.side().m_ActiveObject == objects.front() ? 1 : 0);
        clearSelection();
    }

    void inspect(bench::Counters &details) override {
        details.add("visible_objects", visibleObjects);
        details.add("rng_draws", drawsSinceSeed(benchmarkSeed));
    }

private:
    void clearSelection() {
        SelectionFixture::deleteSelections();
        world.side().GetCurSelGroup()->RemoveAll();
        world.side().SetCurGroup(nullptr);
        world.side().Select(NOTHING, nullptr);
    }

    World world;
    CFormMatrixGame form;
    std::vector<ScreenBuilding *> objects;
    std::int64_t rectangleTests = 0;
};

// ---------------------------------------------------------------------------
// simulation.side_logic_s10: the side catch-up of CMatrixMapLogic::Takt under
// the 10 ms schedule for a crowd of idle player robots in several groups.

constexpr int sideRobots = 96;
constexpr int sideGroups = 6;
constexpr int sideFrames = 100;
constexpr EMatrixPlayerOrder groupOrders[sideGroups] = {mpo_Stop,       mpo_AutoAttack, mpo_AutoCapture,
                                                        mpo_AutoDefence, mpo_Stop,       mpo_AutoAttack};

class SideLogicSession final : public bench::Session {
public:
    SideLogicSession() {
        // The player tick shows the cursor; the software cursor avoids the desktop cursor and device.
        g_Config.m_SoftwareCursor = true;
        for (int i = 0; i < sideRobots; ++i) {
            const CPoint cell(4 + (i % 12) * 2, 4 + (i / 12) * 6);
            robots.emplace_back(HNew(g_MatrixHeap) IdleRobot(i % sideGroups, cell));
        }
        for (int g = 0; g < sideGroups; ++g) {
            auto &group = world.side().m_PlayerGroup[g];
            group.Order(groupOrders[g]);
            group.m_To = CPoint(-1, -1);
            group.m_Obj = nullptr;
        }
    }

    ~SideLogicSession() override { robots.clear(); }

    void run(bench::Counters &work) override {
        auto &map = *world.map;
        auto &side = world.side();
        for (int frame = 0; frame < sideFrames; ++frame) {
            map.advance(LOGIC_TAKT_PERIOD);
            // Same catch-up as CMatrixMapLogic::Takt; object and presentation passes are excluded.
            while (map.GetTime() > map.m_TaktNext) {
                map.m_TaktNext += LOGIC_TAKT_PERIOD;
                side.LogicTakt(LOGIC_TAKT_PERIOD);
                work.add("side_ticks", 1);
            }
        }
        work.add("frames", sideFrames);
        work.add("counted_robots", side.GetRobotsCnt());
    }

    void inspect(bench::Counters &details) override {
        details.add("robots", sideRobots);
        details.add("groups", sideGroups);
        details.add("map_time_ms", world.map->GetTime());
        details.add("rng_draws", drawsSinceSeed(benchmarkSeed));
    }

private:
    World world;
    std::vector<EnginePtr<IdleRobot>> robots;
};

const bench::Workload workloads[] = {
        {"navigation.crowd_move",
         "One player move order for a 48-robot group across a walled 128x128 movement grid: place assignment, "
         "group evaluation and each robot's zone route and local path",
         {{"robots", std::to_string(crowdRobots)},
          {"grid_cells", "128x128"},
          {"zone_cells", "8x8"},
          {"chassis", "wheel"},
          {"seed", std::to_string(benchmarkSeed)}},
         20,
         [] { return std::make_unique<CrowdMoveSession>(); }},
        {"selection.drag_refresh",
         "One 60-frame rectangle drag with four queued mouse moves per frame and a pending refresh over 600 visible "
         "objects, released through the game form",
         {{"visible_objects", std::to_string(visibleObjects)},
          {"frames", std::to_string(dragFrames)},
          {"moves_per_frame", "4"},
          {"seed", std::to_string(benchmarkSeed)}},
         100,
         [] { return std::make_unique<SelectionDragSession>(); }},
        {"simulation.side_logic_s10",
         "Player-side logic catch-up for 100 frames of the 10 ms schedule with 96 idle robots in six ordered groups",
         {{"robots", std::to_string(sideRobots)},
          {"groups", std::to_string(sideGroups)},
          {"frames", std::to_string(sideFrames)},
          {"schedule", "S10"},
          {"seed", std::to_string(benchmarkSeed)}},
         40,
         [] { return std::make_unique<SideLogicSession>(); }},
};
}  // namespace

int main(int argc, char **argv) {
    // Failures must end the process without a desktop dialog. BaseInit installs the
    // engine's message-box handlers; replace both before any workload runs.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
#if defined(_MSC_VER) && defined(_DEBUG)
    // Report MSVC Debug runtime failures on the console instead of a dialog.
    tests::configure_crt_reports();
#endif
    int result = bench::run(argc, argv, workloads);
#ifdef MEM_SPY_ENABLE
    if (result == EXIT_SUCCESS && Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Benchmark leaked tracked heap allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    if (result == EXIT_SUCCESS) Base::CMain::BaseDeInit();
    return result;
}
