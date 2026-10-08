// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Helper.hpp"

#include <memory>

// Supply an idle weapon without loading meshes; only command evaluation runs.
struct RobotCommandFixture {
    static void equip_bomb(CMatrixRobotAI &robot) {
        robot.m_Weapons[0].m_Unit = &robot.m_Unit[0];
        robot.m_Weapons[0].CreateEffect(0, nullptr, WEAPON_BIGBOOM);
        robot.m_WeaponsCnt = 1;
    }

    static void release_bomb(CMatrixRobotAI &robot) {
        robot.m_Weapons[0].Release();
        robot.m_WeaponsCnt = 0;
    }
};

namespace {
struct World {
    CHeap heap;
    CBlockPar data;
    std::unique_ptr<CMatrixMapLogic> map;

    World() {
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        try {
            CacheInit();
            map = std::make_unique<CMatrixMapLogic>();
            g_MatrixMap = map.get();
            map->m_SizeMove = CPoint(20, 20);
            map->m_PlayerSide = nullptr;
            map->m_Side = HNew(g_MatrixHeap) CMatrixSideUnit;
            map->m_SideCnt = 1;
            map->m_PlayerSide = map->m_Side;
            map->m_PlayerSide->m_Id = PLAYER_SIDE;
        }
        catch (...) {
            clear();
            throw;
        }
    }

    ~World() { clear(); }

    void clear() {
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
        g_MatrixMap = nullptr;
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
    }
};

struct Bomber : CMatrixRobotAI {
    Bomber() {
        m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
        m_Side = PLAYER_SIDE;
        SetGroupLogic(0);
        RobotCommandFixture::equip_bomb(*this);
        AddLT();
    }

    ~Bomber() {
        // No mesh is loaded, so release the idle effect without its animation hook.
        RobotCommandFixture::release_bomb(*this);
    }
};

struct Scenario {
    World world;
    Bomber robot;
    const CPoint destination{10, 10};

    Scenario() {
        robot.GetEnv()->m_PlaceAdd = destination;
        robot.MoveTo(destination.x, destination.y);
        group().m_RobotCnt = 1;
        group().Order(mpo_Bomb);
        group().m_To = destination;
        group().m_Obj = nullptr;
    }

    SMatrixPlayerGroup &group() { return world.map->GetPlayerSide()->m_PlayerGroup[0]; }

    void evaluate() {
        MG_CHECK(robot.HaveBomb());
        MG_CHECK(!robot.PLIsInPlace());
        world.map->GetPlayerSide()->TaktPL(0);
        MG_CHECK(group().Order() == mpo_Bomb);
        MG_CHECK(group().m_Obj == nullptr);
        MG_CHECK(robot.IsLiveRobot());
        MG_CHECK(robot.GetEnv()->m_PlaceAdd.Dist2(destination) == 0);
        MG_CHECK(g_D3DD == nullptr);
        for (auto *cached : g_Cache->_data) {
            MG_CHECK(!cached->IsLoaded());
        }
    }
};

void ground_target_while_moving() {
    Scenario scenario;
    scenario.evaluate();
}

void inactive_target() {
    Scenario scenario;
    CMatrixRobotAI target;
    target.m_Side = 2;
    target.m_CurrState = ROBOT_DIP;
    target.AddLT();
    scenario.group().m_Obj = &target;
    scenario.evaluate();
}

void destroyed_target() {
    Scenario scenario;
    {
        CMatrixRobotAI target;
        target.m_Side = 2;
        target.AddLT();
        scenario.group().m_Obj = &target;
    }
    scenario.evaluate();
}

constexpr tests::Case cases[] = {
    {"game.bomb.ground_target", ground_target_while_moving},
    {"game.bomb.inactive_target", inactive_target},
    {"game.bomb.destroyed_target", destroyed_target},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Command test leaked tracked heap allocations.\n";
        for (auto *block = Base::SMemHeader::first_mem_block; block; block = block->next) {
            std::cerr << block->file << ':' << block->line << " bytes=" << block->blocksize << '\n';
        }
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
