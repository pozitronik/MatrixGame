// MatrixGame - licensed under GPLv2 or any later version.
#include "native_demo_assets.hpp"
#include "native_session_assets.hpp"
#include "native_outcome_assets.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixRobot.hpp"
#include "CStorage.hpp"

namespace tests {
void write_native_demo_fights(const std::filesystem::path &path) {
    write_native_demo_outcome(path, true);
    CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    const float x = 400, y = 300;
    const BYTE side = 2, zero = 0;
    const int unused = 0;
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT)->AddToArray<float>(0, &x, 1);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT)->AddToArray<float>(0, &y, 1);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE)->AddToArray<BYTE>(0, &side, 1);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE)->AddToArray<BYTE>(0, &zero, 1);
    for (const auto *column : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        map.GetBuf(DATA_ROBOTS, column, ST_INT32)->AddToArray<int>(0, &unused, 1);
    const std::wstring modules = std::to_wstring(MRT_CHASSIS) + L"," + std::to_wstring(RUK_CHASSIS_WHEEL) +
        L",0|" + std::to_wstring(MRT_ARMOR) + L"," + std::to_wstring(RUK_ARMOR_DIPLOID);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR)->AddWStr(modules);
    map.Save(path.c_str());
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Chassis")->ParSetAdd(PAR_SOURCE_CHASSIS2_MOVE_SPEED, L"0");
    data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Armor")->ParSetAdd(PAR_SOURCE_ARMOR6_STRUCTURE, L"30000");
    data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Armor")->ParSetAdd(PAR_SOURCE_ARMOR2_STRUCTURE, L"200000");
    data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_OVERHEAT)
        ->ParSetAdd(PAR_SOURCE_OVERHEAT_WEAPON_LASER_HEAT, L"0");
    data.SaveInTextFile(L"CFG/robots/data.txt");
}
void write_native_demo_outcome(const std::filesystem::path &path, bool player_wins) {
    write_native_outcome_map(path, player_wins);
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_DAMAGES)->BlockGet(PAR_SOURCE_DAMAGES_ROBOT)
        ->ParSetAdd(L"WEAPON_LASER", L"1000,0,0");
    data.SaveInTextFile(L"CFG/robots/data.txt");
}
void write_native_demo_camera(const std::filesystem::path &path) {
    write_native_session_map(path);
    CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    const float x[] = {100, 300, 500}, y[] = {100, 100, 300};
    const BYTE sides[] = {1, 2, 2}, shadows[] = {0, 0, 0};
    const int zeroes[] = {0, 0, 0};
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT)->SetArray<float>(0, x, 3);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT)->SetArray<float>(0, y, 3);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE)->SetArray<BYTE>(0, sides, 3);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE)->SetArray<BYTE>(0, shadows, 3);
    for (const auto *column : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        map.GetBuf(DATA_ROBOTS, column, ST_INT32)->SetArray<int>(0, zeroes, 3);
    auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR);
    units->AddWStr(units->GetAsWStr(0));
    map.Save(path.c_str());
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Chassis")->ParSetAdd(PAR_SOURCE_CHASSIS2_MOVE_SPEED, L"0");
    data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_DAMAGES)->BlockGet(PAR_SOURCE_DAMAGES_ROBOT)
        ->ParSetAdd(L"WEAPON_LASER", L"1,0,0");
    data.SaveInTextFile(L"CFG/robots/data.txt");
}
void write_native_demo_reinforcement(const std::filesystem::path &path, bool eligible) {
    write_native_session_building_assets();
    write_native_session_map(path, true);
    CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    const float x = 400, y = 100;
    const BYTE side = eligible ? PLAYER_SIDE : 2, kind = BUILDING_TITAN, zero = 0;
    const int size = 0;
    map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_X, ST_FLOAT)->SetArray<float>(0, &x, 1);
    map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_Y, ST_FLOAT)->SetArray<float>(0, &y, 1);
    map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE)->SetArray<BYTE>(0, &side, 1);
    map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_KIND, ST_BYTE)->SetArray<BYTE>(0, &kind, 1);
    for (const auto *column : {DATA_BUILDINGS_SHADOW, DATA_BUILDINGS_ANGLE})
        map.GetBuf(DATA_BUILDINGS, column, ST_BYTE)->SetArray<BYTE>(0, &zero, 1);
    map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SHADOWSIZE, ST_INT32)->SetArray<int>(0, &size, 1);
    // The declared 20-second cooldown becomes 200 ms through the map's percentage.
    // No running map timer or automatic-control flag is assigned by the test.
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(DATA_MAINTENANCETIME);
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"1");
    map.Save(path.c_str());

    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(PAR_SOURCE_TIMINGS)->ParSetAdd(PAR_SOURCE_TIMINGS_MAINTENANCE, L"20000");
    auto *flyer = data.BlockGet(L"Models")->BlockGet(L"Flyers")->BlockGet(L"1");
    flyer->Clear();
    auto *body = flyer->BlockAdd(L"Body");
    body->ParSetAdd(L"Model", L"synthetic-transport-body.vo");
    body->ParSetAdd(L"Texture", L"pixel");
    write_battle_mesh(L"synthetic-transport-body.vo", {}, false, L"pixel");
    auto *order = data.BlockGet(PAR_SOURCE_FLYER_ORDERS)->BlockGet(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
    order->Clear();
    order->ParSetAdd(L"Distance", L"300");
    order->ParSetAdd(L"Height", L"100");
    order->ParSetAdd(L"LandHeight", L"40");
    order->ParSetAdd(L"Flyer", L"1");
    order->ParSetAdd(L"Hitpoint", L"1000");
    auto *bot = order->BlockAdd(L"100");
    bot->ParSetAdd(L"BotChassis", L"2");
    bot->ParSetAdd(L"BotArmor", L"1");
    bot->ParSetAdd(L"BotHead", L"0");
    bot->ParSetAdd(L"BotStrength", L"1");
    bot->ParSetAdd(L"BotHitpoint", L"1500");
    data.SaveInTextFile(L"CFG/robots/data.txt");
}
}
