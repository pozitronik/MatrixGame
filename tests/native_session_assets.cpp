// MatrixGame - licensed under GPLv2 or any later version.
#include "native_session_assets.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "CStorage.hpp"
#include <filesystem>
#include <regex>
#include <set>
#include <vector>
#include <cstring>
#include <functional>

namespace fs = std::filesystem;
namespace tests {
namespace {
void table(CStorage &storage, const wchar_t *name,
    std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns, bool arrays = true) {
    CStorageRecord record(name);
    for (const auto &[column, type] : columns) record.AddItem(CStorageRecordItem(column, type));
    storage.AddRecord(record);
    if (arrays) for (const auto &[column, type] : columns)
        if (type != ST_WCHAR) storage.GetBuf(name, column, type)->AddArray();
}
}
void write_native_session_map(const std::filesystem::path &path) {
    CStorage map;
    table(map, DATA_PROPERTIES, {{DATA_PROPERTIES_NAME, ST_WCHAR}, {DATA_PROPERTIES_VALUE, ST_WCHAR}});
    for (const auto *name : {DATA_SIZEX, DATA_SIZEY}) {
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(name);
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"32");
    }
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(DATA_TEXUNIONDIM);
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"1");
    table(map, DATA_POINTS, {{DATA_POINTS_DATA, ST_BYTE}});
    table(map, DATA_MOVE, {{DATA_MOVE_DATA, ST_BYTE}});
    std::vector<SCompilePoint> points(33 * 33);
    for (auto &point : points) { point.z = 10; point.flags = CELLFLAG_LAND | CELLFLAG_FLAT; point.r = point.g = point.b = 128; }
    SCompileMoveCell cell{};
    for (auto &move : cell.c) move.m_Zone = 0;
    map.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(points.data()), points.size() * sizeof(SCompilePoint));
    map.GetBuf(DATA_MOVE, DATA_MOVE_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(&cell), sizeof(cell));
    table(map, DATA_STRINGS, {{DATA_STRINGS_STRING, ST_WCHAR}});
    map.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, ST_WCHAR)->AddWStr(L"pixel.png");
    table(map, DATA_TEXUINIONS, {{DATA_TEXUINIONS_DATA, ST_BYTE}});
    table(map, DATA_BOTTOM, {{DATA_BOTTOM_DATA, ST_INT32}});
    table(map, DATA_BITMAPS, {{DATA_BITMAPS_BITMAP, ST_BYTE}}, false);
    const int tile = 0;
    map.GetBuf(DATA_TEXUINIONS, DATA_TEXUINIONS_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(&tile), sizeof(tile));
    map.GetBuf(DATA_BOTTOM, DATA_BOTTOM_DATA, ST_INT32)->AddToArray<int>(0, &tile, 1);
    table(map, DATA_GROUPS, {{DATA_GROUPS_DATA, ST_BYTE}}, false);
    for (int y = 0; y < 32; y += MAP_GROUP_SIZE) for (int x = 0; x < 32; x += MAP_GROUP_SIZE) {
        std::vector<BYTE> bytes;
        const auto append = [&]<class T>(const T &value) {
            const auto *start = reinterpret_cast<const BYTE *>(&value);
            bytes.insert(bytes.end(), start, start + sizeof(value));
        };
        append(static_cast<WORD>(x)); append(static_cast<WORD>(y));
        append(DWORD{1}); append(DWORD{0}); append(DWORD{12});
        for (WORD index : {0, 1, 2, 1, 3, 2}) append(index);
        append(DWORD{4 * sizeof(SCompileBottomVert)});
        // The last terrain group can be smaller than MAP_GROUP_SIZE.
        const int right = std::min(x + MAP_GROUP_SIZE, 32);
        const int bottom = std::min(y + MAP_GROUP_SIZE, 32);
        for (const auto &point : {CPoint(x,y), CPoint(right,y),
             CPoint(x,bottom), CPoint(right,bottom)}) {
            MG_CHECK(point.x >= 0 && point.x <= 32 && point.y >= 0 && point.y <= 32);
            append(SCompileBottomVert{static_cast<uint16_t>(point.x),static_cast<uint16_t>(point.y),0,0});
        }
        auto *groups = map.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
        const auto index = groups->AddArray();
        groups->AddToArray<BYTE>(index, bytes.data(), bytes.size());
    }
    table(map, DATA_SURFACES, {{DATA_SURFACES_DATA, ST_BYTE}}, false);
    table(map, DATA_SURFACES_M, {{DATA_SURFACES_DATA_M, ST_BYTE}}, false);
    table(map, DATA_OBJECTS, {{DATA_OBJECTS_X, ST_FLOAT}, {DATA_OBJECTS_Y, ST_FLOAT},
        {DATA_OBJECTS_ANGLE_Z, ST_FLOAT}, {DATA_OBJECTS_SCALE, ST_FLOAT}, {DATA_OBJECTS_TYPE, ST_DWORD}});
    table(map, DATA_BUILDINGS, {{DATA_BUILDINGS_X, ST_FLOAT}, {DATA_BUILDINGS_Y, ST_FLOAT},
        {DATA_BUILDINGS_SIDE, ST_BYTE}, {DATA_BUILDINGS_KIND, ST_BYTE}, {DATA_BUILDINGS_SHADOW, ST_BYTE},
        {DATA_BUILDINGS_SHADOWSIZE, ST_INT32}, {DATA_BUILDINGS_ANGLE, ST_BYTE}});
    table(map, DATA_CANNONS, {{DATA_CANNONS_X, ST_FLOAT}, {DATA_CANNONS_Y, ST_FLOAT},
        {DATA_CANNONS_SIDE, ST_BYTE}, {DATA_CANNONS_KIND, ST_BYTE}, {DATA_CANNONS_SHADOW, ST_BYTE},
        {DATA_CANNONS_SHADOWSIZE, ST_INT32}, {DATA_CANNONS_ANGLE, ST_FLOAT}});
    table(map, DATA_ROBOTS, {{DATA_ROBOTS_X, ST_FLOAT}, {DATA_ROBOTS_Y, ST_FLOAT},
        {DATA_ROBOTS_SIDE, ST_BYTE}, {DATA_ROBOTS_SHADOW, ST_BYTE}, {DATA_ROBOTS_SHADOWSIZE, ST_INT32},
        {DATA_ROBOTS_GROUP, ST_INT32}, {DATA_ROBOTS_UNITS, ST_WCHAR}});
    const float coordinates[] = {100, 500};
    for (const auto *column : {DATA_ROBOTS_X, DATA_ROBOTS_Y})
        map.GetBuf(DATA_ROBOTS, column, ST_FLOAT)->AddToArray<float>(0, coordinates, 2);
    const BYTE sides[] = {1, 2}, shadows[] = {0, 0};
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE)->AddToArray<BYTE>(0, sides, 2);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE)->AddToArray<BYTE>(0, shadows, 2);
    const int zeroes[] = {0, 0};
    for (const auto *column : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        map.GetBuf(DATA_ROBOTS, column, ST_INT32)->AddToArray<int>(0, zeroes, 2);
    const std::wstring modules = std::to_wstring(MRT_CHASSIS) + L"," + std::to_wstring(RUK_CHASSIS_WHEEL) +
        L",0|" + std::to_wstring(MRT_ARMOR) + L"," + std::to_wstring(RUK_ARMOR_MONOSTACK);
    for (int i = 0; i < 2; ++i) map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR)->AddWStr(modules.c_str());

    CMatrixRoadNetwork road(nullptr);
    road.SetZoneCntMax(1); road.m_ZoneCnt = 1;
    road.m_Zone[0].m_Rect = CRect(0, 0, 64, 64);
    road.m_Zone[0].m_Center = CPoint(32, 32);
    road.m_Zone[0].m_Size = 64 * 64;
    road.m_Zone[0].m_Region = 0;
    road.m_Region = static_cast<SMatrixRegion *>(HAllocClear(sizeof(SMatrixRegion), nullptr));
    road.m_RegionCnt = 1;
    road.m_Region[0].m_Center = CPoint(32, 32);
    road.m_Region[0].m_RadiusPlace = 64;
    road.SetPlaceCnt(4);
    for (int i = 0; i < 4; ++i) {
        auto &place = road.m_Place[i];
        place.m_Pos = CPoint((i & 1) ? 48 : 8, (i & 2) ? 48 : 8);
        place.m_Region = 0; place.m_Empty = false; place.m_NearCnt = 2;
        place.m_Near[0] = i ^ 1; place.m_Near[1] = i ^ 2;
        road.m_Zone[0].m_Place[i] = road.m_Region[0].m_Place[i] = road.m_Region[0].m_PlaceAll[i] = i;
    }
    road.m_Zone[0].m_PlaceCnt = 4;
    road.m_Region[0].m_PlaceCnt = road.m_Region[0].m_PlaceAllCnt = 4;
    CBuf network;
    network.Add<DWORD>(27);
    road.Save(network);
    table(map, DATA_ROADS, {{DATA_ROADS_DATA, ST_BYTE}});
    map.GetBuf(DATA_ROADS, DATA_ROADS_DATA, ST_BYTE)->AddToArray<BYTE>(0, network.Buff<BYTE>(), network.Len());
    map.Save(path.c_str());
}
void write_native_session_assets() {
    fs::create_directories(L"CFG/robots");
    const fs::path source(MATRIXGAME_SOURCE_ROOT);
    fs::copy_file(source / L"MatrixGame/CFG/robots/data.txt", L"CFG/robots/data.txt");
    fs::copy_file(source / L"MatrixGame/CFG/robots/iface.txt", L"CFG/robots/iface.txt");
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(L"Config")->ParSetAdd(L"SoftwareCursor", L"1");
    data.SaveInTextFile(L"CFG/robots/data.txt");
    CBitmap bitmap;
    bitmap.CreateRGBA(1024, 1024);
    std::memset(bitmap.Data(), 128, bitmap.Pitch() * bitmap.SizeY());
    bitmap.SaveInPNG(L"pixel.png");
    // Configured paths name synthetic replacement images; no original assets are read.
    std::set<std::string> paths;
    const std::regex pattern(R"(Matrix[\\/][A-Za-z0-9_./\\-]+)", std::regex::icase);
    const std::function<void(CBlockPar &)> collect = [&](CBlockPar &block) {
        for (int i = 0; i < block.ParCount(); ++i) {
            const std::string text = utils::from_wstring(block.ParGet(i));
            for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
                paths.insert(it->str());
        }
        for (int i = 0; i < block.BlockCount(); ++i) collect(*block.BlockGet(i));
    };
    collect(data);
    CBlockPar interface_config;
    interface_config.LoadFromTextFile(L"CFG/robots/iface.txt");
    collect(interface_config);
    const std::wstring extra = L"W," + std::to_wstring(RUK_WEAPON_BOMB);
    const tests::MeshAttachment attachments[] = {{1,L"root",0,0,20}, {2,L"left",-8,0,0}, {3,L"right",8,0,0},
        {20,L"W,1",-8,0,20}, {21,L"W,1",8,0,20}, {22,L"W,1",-8,8,20}, {23,L"W,1",8,8,20},
        {24,extra.c_str(),0,0,20}};
    for (const auto *texture : {TEXTURE_PATH_REFLECTION, TEXTURE_PATH_SPOT, TEXTURE_PATH_OBJSEL,
        TEXTURE_PATH_HIT, TEXTURE_PATH_POINTLIGHT, TEXTURE_PATH_VORONKA, TEXTURE_PATH_TURRET_RADIUS,
        TEXTURE_PATH_SOLE_TRACK, TEXTURE_PATH_SOLE_WHEEL, TEXTURE_PATH_SOLE_PNEUMATIC, TEXTURE_PATH_PB,
        TEXTURE_PATH_MOVETO, TEXTURE_PATH_DUST, TEXTURE_PATH_BIGBOOM, TEXTURE_PATH_GUN_FIRE,
        TEXTURE_PATH_GUN_BULLETS1, TEXTURE_PATH_GUN_BULLETS2, TEXTURE_PATH_SPLASH,
        TEXTURE_PATH_KEELWATER, TEXTURE_PATH_WAVE, TEXTURE_PATH_ZAHVAT})
        paths.insert(utils::from_wstring(texture));
    for (const auto &path : paths) {
        fs::path target(path);
        if (_wcsicmp(target.extension().c_str(), L".vo") == 0) {
            fs::create_directories(target.parent_path());
            tests::write_battle_mesh(target, attachments);
        }
        if (target.has_extension()) continue;
        target += L".png";
        fs::create_directories(target.parent_path());
        if (!fs::exists(target)) fs::copy_file(L"pixel.png", target);
    }
    fs::create_directories(L"Matrix/Robot");
    for (const auto &[prefix, count] : {std::pair{OBJECT_PATH_ROBOT_HEAD, ROBOT_HEAD_CNT},
        {OBJECT_PATH_ROBOT_CHASSIS, ROBOT_CHASSIS_CNT}, {OBJECT_PATH_ROBOT_WEAPON, ROBOT_WEAPON_CNT},
        {OBJECT_PATH_ROBOT_ARMOR, ROBOT_ARMOR_CNT}}) {
        for (unsigned i = 1; i <= count; ++i) {
            tests::write_battle_mesh(std::wstring(prefix) + std::to_wstring(i) + L".vo", attachments,
                wcscmp(prefix, OBJECT_PATH_ROBOT_CHASSIS) == 0 && i == RUK_CHASSIS_PNEUMATIC, L"pixel");
            const fs::path texture = std::wstring(prefix) + std::to_wstring(i) + L".png";
            if (!fs::exists(texture)) fs::copy_file(L"pixel.png", texture);
        }
    }
    write_native_session_map(L"synthetic.map");
}
}
