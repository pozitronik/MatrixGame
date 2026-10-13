// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "CStorage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace tests {
namespace {
void schema(CStorage &storage, const wchar_t *table,
            std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns) {
    CStorageRecord record(table);
    for (const auto &[name, type] : columns) record.AddItem(CStorageRecordItem(name, type));
    storage.AddRecord(record);
}

template<class T> void bytes(CStorage &storage, const wchar_t *table,
                            const wchar_t *column, const T *values, size_t count) {
    auto *buffer = storage.GetBuf(table, column, ST_BYTE);
    buffer->AddArray();
    if (count) buffer->AddToArray<BYTE>(0, reinterpret_cast<const BYTE *>(values),
                                      static_cast<int>(count * sizeof(T)));
}

void numbers(CStorage &storage, const wchar_t *table, const wchar_t *column,
             const std::vector<DWORD> &values) {
    auto *buffer = storage.GetBuf(table, column, ST_DWORD);
    buffer->AddArray();
    buffer->AddToArray<DWORD>(0, values.data(), static_cast<int>(values.size()));
}

// A genuine storage-format mesh, with CPU geometry and animation. No texture or
// private resource is required. Its file is needed only during the eager load.
struct MeshFile {
    std::filesystem::path path;
    MeshFile() {
        static unsigned sequence = 0;
        path = std::filesystem::current_path() /
            ("battle-chassis-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(++sequence) + ".vo");
        MG_CHECK(!std::filesystem::exists(path));
    }
    ~MeshFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

void write_mesh(const std::filesystem::path &path, std::span<const MeshAttachment> attachments, bool combat_animations, float height) {
    CStorage storage;
    schema(storage, L"verts", {{L"data", ST_BYTE}});
    schema(storage, L"idxs", {{L"data", ST_BYTE}});
    schema(storage, L"unions", {{L"data", ST_BYTE}});
    schema(storage, L"frames", {{L"data", ST_BYTE}, {L"verts", ST_BYTE},
                                {L"edges", ST_BYTE}, {L"unions", ST_BYTE}});
    schema(storage, L"anims", {{L"name", ST_WCHAR}, {L"id", ST_DWORD},
                               {L"disp", ST_DWORD}, {L"cnt", ST_DWORD}, {L"frames", ST_BYTE}});
    schema(storage, L"matrices", {{L"name", ST_WCHAR}, {L"id", ST_DWORD},
                                  {L"disp", ST_DWORD}, {L"data", ST_BYTE}});

    std::array<SVOVertex, 8> vertices{};
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].v = D3DXVECTOR3((i & 1) ? 12.0f : -12.0f,
                                   (i & 2) ? 12.0f : -12.0f, (i & 4) ? height : 0.0f);
        vertices[i].n = D3DXVECTOR3(0, 0, 1);
    }
    const WORD indices[] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6,
                            0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7,
                            0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    bytes(storage, L"verts", L"data", vertices.data(), vertices.size());
    bytes(storage, L"idxs", L"data", indices, std::size(indices));
    SVOUnion surface{};
    surface.m_VerCnt = 8;
    surface.m_TriCnt = 12;
    bytes(storage, L"unions", L"data", &surface, 1);
    SVOKadr frame{};
    frame.m_Min = D3DXVECTOR3(-12, -12, 0);
    frame.m_Max = D3DXVECTOR3(12, 12, height);
    frame.m_GeoCenter = D3DXVECTOR3(0, 0, height / 2);
    frame.m_Radius = height == 20.0f ? 20.0f : std::sqrt(288.0f + height * height / 4.0f);
    frame.m_VerCnt = 8;
    frame.m_UnionCnt = 1;
    bytes(storage, L"frames", L"data", &frame, 1);
    const int vertex_indices[] = {0, 1, 2, 3, 4, 5, 6, 7};
    const DWORD union_index = 0;
    bytes(storage, L"frames", L"verts", vertex_indices, std::size(vertex_indices));
    bytes(storage, L"frames", L"unions", &union_index, 1);
    bytes<int>(storage, L"frames", L"edges", nullptr, 0);

    std::vector<const wchar_t *> animations = {ANIMATION_NAME_IDLE, ANIMATION_NAME_STAY,
        ANIMATION_NAME_MOVE, ANIMATION_NAME_MOVE_BACK, ANIMATION_NAME_ROTATE};
    if (combat_animations) { animations.push_back(ANIMATION_NAME_FIRE); animations.push_back(ANIMATION_NAME_FIRELOOP); }
    std::vector<DWORD> ids, offsets, counts;
    std::vector<SVOFrameIndex> frames;
    for (size_t i = 0; i < std::size(animations); ++i) {
        storage.GetBuf(L"anims", L"name", ST_WCHAR)->AddWStr(animations[i]);
        ids.push_back(static_cast<DWORD>(i));
        offsets.push_back(static_cast<DWORD>(i));
        counts.push_back(1);
        frames.push_back({0, 100});
    }
    numbers(storage, L"anims", L"id", ids);
    numbers(storage, L"anims", L"disp", offsets);
    numbers(storage, L"anims", L"cnt", counts);
    bytes(storage, L"anims", L"frames", frames.data(), frames.size());
    std::vector<DWORD> matrix_ids, matrix_offsets;
    std::vector<D3DXMATRIX> matrices;
    for (const auto &item : attachments) {
        storage.GetBuf(L"matrices", L"name", ST_WCHAR)->AddWStr(item.name);
        matrix_ids.push_back(item.id);
        matrix_offsets.push_back(static_cast<DWORD>(matrices.size()));
        D3DXMATRIX matrix;
        D3DXMatrixIdentity(&matrix);
        matrix._41 = item.x; matrix._42 = item.y; matrix._43 = item.z;
        matrices.push_back(matrix);
    }
    numbers(storage, L"matrices", L"id", matrix_ids);
    numbers(storage, L"matrices", L"disp", matrix_offsets);
    bytes(storage, L"matrices", L"data", matrices.data(), matrices.size());
    storage.Save(path.c_str());
}

}
CVectorObject *load_battle_mesh(std::span<const MeshAttachment> attachments, const wchar_t *cache_name, bool combat_animations, float height) {
    MeshFile file;
    MG_CHECK(std::isfinite(height) && height > 0 && height <= 1000);
    write_mesh(file.path, attachments, combat_animations, height);
    auto *geometry = static_cast<CVectorObject *>(g_Cache->Get(CacheClass::VO, file.path.c_str()));
    geometry->LoadSpecial(OLF_NO_TEX, nullptr, 0);
    MG_CHECK(std::filesystem::remove(file.path));
    if (cache_name) {
        MG_CHECK(g_Cache->Find(CacheClass::VO, cache_name) == nullptr);
        geometry->m_Name = cache_name;
    }
    return geometry;
}
}

namespace tests {
namespace {
template<class T> T *allocate(size_t count) {
    return static_cast<T *>(HAllocClear(count * sizeof(T), g_MatrixHeap));
}

template<class T> void append(std::vector<BYTE> &output, const T &value) {
    const auto *begin = reinterpret_cast<const BYTE *>(&value);
    output.insert(output.end(), begin, begin + sizeof(value));
}

void build_group(CDataBuf &groups, int x, int y) {
    const int w = std::min(MAP_GROUP_SIZE, 32 - x);
    const int h = std::min(MAP_GROUP_SIZE, 32 - y);
    std::vector<BYTE> data;
    append(data, static_cast<WORD>(x));
    append(data, static_cast<WORD>(y));
    append(data, DWORD{1}); // One material batch, never drawn in this profile.
    append(data, DWORD{0});
    append(data, DWORD{12});
    for (WORD index : {WORD{0}, WORD{1}, WORD{2}, WORD{1}, WORD{3}, WORD{2}}) append(data, index);
    append(data, DWORD{4 * sizeof(SCompileBottomVert)});
    for (const auto &point : {CPoint(x, y), CPoint(x + w, y), CPoint(x, y + h), CPoint(x + w, y + h)}) {
        const SCompileBottomVert vertex{static_cast<uint16_t>(point.x), static_cast<uint16_t>(point.y), 0, 0};
        append(data, vertex);
    }
    const int array = groups.AddArray();
    groups.AddToArray<BYTE>(array, data.data(), static_cast<int>(data.size()));
}
}

void BattleGeometry::configure(CMatrixMapLogic &map, NavigationLayout layout) {
    MG_CHECK(g_MatrixMap == &map);
    MG_CHECK(map.m_Unit == nullptr && map.m_Move == nullptr && map.m_Point == nullptr);
    map.m_Size = CPoint(32, 32);
    map.m_SizeMove = CPoint(movement_size, movement_size);
    map.m_Unit = allocate<SMatrixMapUnit>(32 * 32);
    map.m_Point = allocate<SMatrixMapPoint>(33 * 33);
    map.m_Move = allocate<SMatrixMapMove>(movement_size * movement_size);
    for (int i = 0; i < 32 * 32; ++i) map.m_Unit[i].SetType(CELLFLAG_LAND | CELLFLAG_FLAT);
    for (int i = 0; i < 33 * 33; ++i) {
        map.m_Point[i].z = map.m_Point[i].z_land = 10;
        map.m_Point[i].n = D3DXVECTOR3(0, 0, 1);
        map.m_Point[i].color = 0x007f7f7f;
    }
    for (int y = 0; y < movement_size; ++y) for (int x = 0; x < movement_size; ++x) {
        auto &cell = *map.MoveGet(x, y);
        cell.m_Zone = 0;
        for (int size = 1; size <= 5; ++size) {
            const auto overlaps = [&](int left, int top, int right, int bottom) {
                return x < right && x + size > left && y < bottom && y + size > top;
            };
            bool obstacle = false;
            switch (layout) {
            case NavigationLayout::CentralObstacle:
            case NavigationLayout::MultiRegion:
                obstacle = overlaps(obstacle_left, obstacle_top, obstacle_right, obstacle_bottom);
                break;
            case NavigationLayout::Open: break;
            case NavigationLayout::Corridor:
                obstacle = overlaps(28, 2, 36, 28) || overlaps(28, 32, 36, 62);
                break;
            case NavigationLayout::DeadEnd:
                obstacle = overlaps(24, 16, 40, 20) || overlaps(24, 20, 28, 44) || overlaps(36, 20, 40, 44);
                break;
            case NavigationLayout::Disconnected:
                obstacle = overlaps(28, 2, 36, 62);
                break;
            }
            const bool border = x < 2 || y < 2 || x + size > movement_size - 2 || y + size > movement_size - 2;
            if (obstacle || border) cell.m_Stop |= DWORD{31} << (6 * (size - 1));
        }
    }

    auto &road = map.m_RN;
    road.SetZoneCntMax(1);
    road.m_ZoneCnt = 1;
    road.m_Zone[0].m_Rect = CRect(2, 2, 62, 62);
    road.m_Zone[0].m_Center = CPoint(16, 32);
    road.m_Zone[0].m_Size = 60 * 60 - 8 * 24;
    road.m_Zone[0].m_Region = 0;
    road.m_Region = allocate<SMatrixRegion>(1);
    road.m_RegionCnt = 1;
    road.m_Region[0].m_Center = CPoint(16, 32);
    road.m_Region[0].m_RadiusPlace = 64;
    const CPoint places[] = {{8, 8}, {48, 8}, {8, 48}, {48, 48}};
    road.SetPlaceCnt(static_cast<int>(std::size(places)));
    for (int i = 0; i < static_cast<int>(std::size(places)); ++i) {
        road.m_Place[i].m_Pos = places[i];
        road.m_Place[i].m_Region = 0;
        road.m_Place[i].m_Empty = false;
        // Connections follow the four unobstructed edges around the obstacle.
        road.m_Place[i].m_NearCnt = 2;
        road.m_Place[i].m_Near[0] = i ^ 1;
        road.m_Place[i].m_Near[1] = i ^ 2;
        road.m_Zone[0].m_Place[i] = i;
        road.m_Region[0].m_Place[i] = i;
        road.m_Region[0].m_PlaceAll[i] = i;
    }
    road.m_Zone[0].m_PlaceCnt = 4;
    road.m_Region[0].m_PlaceCnt = road.m_Region[0].m_PlaceAllCnt = 4;
    if (layout == NavigationLayout::Disconnected) {
        // A closed barrier separates both the movement grid and the road graph.
        // Keeping one region here would manufacture an invalid-map retry case.
        road.SetZoneCntMax(2);
        road.m_ZoneCnt = 2;
        HFree(road.m_Region, g_MatrixHeap);
        road.m_Region = allocate<SMatrixRegion>(2);
        road.m_RegionCnt = 2;
        for (int region = 0; region < 2; ++region) {
            road.m_Zone[region] = SMatrixMapZone{};
            road.m_Zone[region].m_Rect = region == 0 ? CRect(2, 2, 28, 62) : CRect(36, 2, 62, 62);
            road.m_Zone[region].m_Center = CPoint(region == 0 ? 16 : 48, 32);
            road.m_Zone[region].m_Size = 26 * 60;
            road.m_Zone[region].m_Region = region;
            road.m_Region[region].m_Center = road.m_Zone[region].m_Center;
            road.m_Region[region].m_RadiusPlace = 64;
            for (int i = 0; i < 2; ++i) {
                const int index = region + i * 2;
                road.m_Place[index].m_Region = region;
                road.m_Place[index].m_NearCnt = 1;
                road.m_Place[index].m_Near[0] = index ^ 2;
                road.m_Zone[region].m_Place[i] = index;
                road.m_Region[region].m_Place[i] = index;
                road.m_Region[region].m_PlaceAll[i] = index;
            }
            road.m_Zone[region].m_PlaceCnt = 2;
            road.m_Region[region].m_PlaceCnt = road.m_Region[region].m_PlaceAllCnt = 2;
        }
        for (int y = 0; y < movement_size; ++y) for (int x = 0; x < movement_size; ++x)
            map.MoveGet(x, y)->m_Zone = x >= 36 ? 1 : 0;
    }
    if (layout == NavigationLayout::Corridor) {
        road.m_Place[0].m_Pos = CPoint(8, 28);
        road.m_Place[1].m_Pos = CPoint(48, 28);
        // The upper pair is connected through the gap; the lower pair only connects vertically.
        road.m_Place[2].m_NearCnt = road.m_Place[3].m_NearCnt = 1;
        road.m_Place[2].m_Near[0] = 0;
        road.m_Place[3].m_Near[0] = 1;
    }
    if (layout == NavigationLayout::MultiRegion) {
        // Four quadrants surround the central obstacle; left/right pairs form regions.
        // Keep the physical grid identical to CentralObstacle and supply all hierarchy levels.
        road.SetZoneCntMax(4);
        road.m_ZoneCnt = 4;
        HFree(road.m_Region, g_MatrixHeap);
        road.m_Region = allocate<SMatrixRegion>(2);
        road.m_RegionCnt = 2;
        std::array<CMatrixCrotch *, 4> junctions{};
        for (int i = 0; i < 4; ++i) {
            const int side = i % 2;
            const int row = i / 2;
            auto &zone = road.m_Zone[i];
            zone = {};
            zone.m_Rect = CRect(side ? 32 : 2, row ? 32 : 2, side ? 62 : 32, row ? 62 : 32);
            zone.m_Center = road.m_Place[i].m_Pos;
            zone.m_Size = 30 * 30 - 4 * 12;
            zone.m_Region = side;
            zone.m_PlaceCnt = 1;
            zone.m_Place[0] = i;
            road.m_Place[i].m_Region = side;
            auto &region = road.m_Region[side];
            region.m_Center = CPoint(side ? 48 : 16, 32);
            region.m_RadiusPlace = 64;
            region.m_NearCnt = 1;
            region.m_Near[0] = 1 - side;
            region.m_Place[region.m_PlaceCnt++] = i;
            region.m_PlaceAll[region.m_PlaceAllCnt++] = i;
            junctions[i] = road.AddCrotch();
            junctions[i]->m_Center = zone.m_Center;
            junctions[i]->m_Region = side;
            junctions[i]->m_Zone = i;
            region.m_Crotch[region.m_CrotchCnt++] = junctions[i];
        }
        for (auto endpoints : {std::pair{0, 1}, {0, 2}, {1, 3}, {2, 3}}) {
            const auto [a, b] = endpoints;
            auto &first = road.m_Zone[a];
            auto &second = road.m_Zone[b];
            first.m_NearZone[first.m_NearZoneCnt] = b;
            first.m_NearZoneConnectSize[first.m_NearZoneCnt++] = (a ^ b) == 1 ? 18 : 26;
            second.m_NearZone[second.m_NearZoneCnt] = a;
            second.m_NearZoneConnectSize[second.m_NearZoneCnt++] = (a ^ b) == 1 ? 18 : 26;
            auto *edge = road.AddRoad();
            edge->m_Start = junctions[a]; edge->m_End = junctions[b];
            edge->m_Start->AddRoad(edge); edge->m_End->AddRoad(edge);
            edge->AddPath(first.m_Center.x, first.m_Center.y);
            edge->AddPath(second.m_Center.x, second.m_Center.y);
            edge->AddZone(a); edge->AddZone(b);
            edge->CalcDist();
            MG_CHECK(edge->m_Dist == 40);
        }
        for (int y = 0; y < movement_size; ++y) for (int x = 0; x < movement_size; ++x)
            map.MoveGet(x, y)->m_Zone = (x >= 32 ? 1 : 0) + (y >= 32 ? 2 : 0);
    }
    road.InitPL(movement_size, movement_size);
    map.m_TexUnionDim = 1;
    CStorage groups;
    schema(groups, DATA_GROUPS, {{DATA_GROUPS_DATA, ST_BYTE}});
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        build_group(*groups.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE),
                    x * MAP_GROUP_SIZE, y * MAP_GROUP_SIZE);
    }
    map.GroupBuild(groups);
    if (layout == NavigationLayout::CentralObstacle) {
        MG_CHECK(map.MoveGet(30, 30)->GetType(RUK_CHASSIS_WHEEL - 1) == 0);
        MG_CHECK(map.MoveGet(10, 10)->GetType(RUK_CHASSIS_WHEEL - 1) == 0xff);
        const int chassis = RUK_CHASSIS_WHEEL - 1;
        MG_CHECK(!map.CanMoveFromTo(chassis, 4, 8, 28, 48, 28, nullptr));
        MG_CHECK(map.CanMoveFromTo(chassis, 4, 8, 8, 48, 8, nullptr));
        CPoint path[MatrixPathMoveMax];
        int zone = 0;
        const int count = map.FindLocalPath(chassis, 4, 8, 28, &zone, 1, 48, 28, path,
                                           0, nullptr, nullptr, nullptr, nullptr, false);
        MG_CHECK(count > 2);
        for (int i = 1; i < count; ++i)
            MG_CHECK(map.CanMoveFromTo(chassis, 4, path[i - 1].x, path[i - 1].y,
                                      path[i].x, path[i].y, nullptr));
    }
}

CMatrixRobotAI *BattleGeometry::create_robot(CMatrixMapLogic &map, int side, float x, float y) {
    return create_robot(map, side, x, y, RUK_CHASSIS_WHEEL);
}

CMatrixRobotAI *BattleGeometry::create_robot(CMatrixMapLogic &map, int side, float x, float y, ERobotUnitKind chassis) {
    MG_CHECK(chassis == RUK_CHASSIS_WHEEL || chassis == RUK_CHASSIS_TRACK);
    const MeshAttachment attachment{1, L"chassis-link", 0, 0, 20};
    auto *geometry = load_battle_mesh(std::span(&attachment, 1));
    auto *robot = map.StaticAdd<CMatrixRobotAI>();
    robot->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
    robot->m_Side = side;
    robot->m_PosX = x;
    robot->m_PosY = y;
    robot->m_Forward = robot->m_HullForward = D3DXVECTOR3(1, 0, 0);
    robot->UnitInsert(0, MRT_CHASSIS, chassis);
    robot->m_Unit[0].m_Graph = HNew(g_MatrixHeap) CVectorObjectAnim;
    robot->m_Unit[0].m_Graph->Init(geometry, nullptr);
    robot->m_Unit[0].m_Graph->FirstFrame();
    robot->m_Unit[0].m_Graph->SetAnimByName(ANIMATION_NAME_STAY);
    robot->CalcRobotMass();
    robot->m_GroupSpeed = robot->GetMaxSpeed();
    robot->MapPosCalc();
    robot->RNeed(MR_Matrix);
    robot->JoinToGroup();
    return robot;
}
}
