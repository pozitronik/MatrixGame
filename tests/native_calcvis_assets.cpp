// MatrixGame - licensed under GPLv2 or any later version.
#include "native_calcvis_assets.hpp"
#include "native_session_assets.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "CStorage.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <utility>
#include <cstdint>

namespace tests {
namespace {
constexpr int size = 32;
float height(int x, bool ridge) {
    return ridge ? 10.0f + 16.0f * std::max(0, 8 - std::abs(x - 16)) : 10.0f;
}
}

void write_native_calcvis_map(const std::filesystem::path &path, bool ridge) {
    write_native_session_map(path);
    CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    std::vector<SCompilePoint> points((size + 1) * (size + 1));
    for (int y = 0; y <= size; ++y)
        for (int x = 0; x <= size; ++x) {
            auto &point = points[x + y * (size + 1)];
            point.z = height(x, ridge);
            point.flags = CELLFLAG_LAND | (ridge ? 0 : CELLFLAG_FLAT);
            point.r = point.g = point.b = 128;
        }
    map.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE)->SetArray<BYTE>(
            0, reinterpret_cast<const BYTE *>(points.data()), int(points.size() * sizeof(SCompilePoint)));
    for (const auto &[key, value] : {std::pair{DATA_CAMPOSX, L"120"}, {DATA_CAMPOSY, L"320"}, {DATA_CAMANGLE, L"0"}}) {
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(key);
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(value);
    }
    // Render the authored ridge too; the generic fixture's four corner vertices are all flat.
    auto *groups = map.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
    int group = 0;
    for (int y = 0; y < size; y += MAP_GROUP_SIZE)
        for (int x = 0; x < size; x += MAP_GROUP_SIZE, ++group) {
            const int width = std::min(MAP_GROUP_SIZE, size - x), height = std::min(MAP_GROUP_SIZE, size - y);
            std::vector<BYTE> bytes;
            const auto append = [&]<class T>(const T &value) {
                const auto *p = reinterpret_cast<const BYTE *>(&value);
                bytes.insert(bytes.end(), p, p + sizeof(T));
            };
            append(WORD(x));
            append(WORD(y));
            append(DWORD(1));
            append(DWORD(0));
            append(DWORD(width * height * 6 * sizeof(WORD)));
            for (int row = 0; row < height; ++row)
                for (int column = 0; column < width; ++column) {
                    const WORD a = WORD(column + row * (width + 1)), b = WORD(a + 1),
                               c = WORD(a + width + 1), d = WORD(c + 1);
                    for (WORD index : {a, b, c, b, d, c}) append(index);
                }
            append(DWORD((width + 1) * (height + 1) * sizeof(SCompileBottomVert)));
            for (int row = 0; row <= height; ++row)
                for (int column = 0; column <= width; ++column)
                    append(SCompileBottomVert{uint16_t(x + column), uint16_t(y + row),
                                              uint16_t(column * 16), uint16_t(row * 16)});
            groups->SetArray<BYTE>(group, bytes.data(), int(bytes.size()));
        }
    map.Save(path.c_str());
}

void configure_native_calcvis() {
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    auto *camera = data.BlockGet(PAR_SOURCE_CAMERA);
    camera->ParSetAdd(PAR_SOURCE_CAMERA_BASEANGLEZ, L"0");
    camera->ParSetAdd(PAR_SOURCE_CAMERA_MOVESPEED, L"0");
    auto *strategy = camera->BlockGet(PAR_SOURCE_CAMERA_STRATEGY);
    for (const auto &[key, value] : {std::pair{PAR_SOURCE_CAMERA_DISTMIN, L"70"}, {PAR_SOURCE_CAMERA_DISTMAX, L"70"},
                                   {PAR_SOURCE_CAMERA_ROTANGLEMIN, L"45"}, {PAR_SOURCE_CAMERA_ROTANGLEMAX, L"45"},
                                   {PAR_SOURCE_CAMERA_HEIGHT, L"70"}, {PAR_SOURCE_CAMERA_ROTSPEEDX, L"0"},
                                   {PAR_SOURCE_CAMERA_ROTSPEEDZ, L"0"}, {PAR_SOURCE_CAMERA_WHEELSTEP, L"0"}})
        strategy->ParSetAdd(key, value);
    auto *flyer = data.BlockGet(L"Models")->BlockGet(L"Flyers")->BlockGet(L"1");
    flyer->Clear();
    auto *body = flyer->BlockAdd(L"Body");
    body->ParSetAdd(L"Model", L"synthetic-calcvis-flyer.vo");
    body->ParSetAdd(L"Texture", L"pixel");
    write_battle_mesh(L"synthetic-calcvis-flyer.vo", {}, false, L"pixel");
    data.SaveInTextFile(L"CFG/robots/data.txt");
}

void validate_native_calcvis_configuration(CBlockPar &data) {
    auto *camera = data.BlockGet(PAR_SOURCE_CAMERA);
    MG_CHECK(camera->ParGet(PAR_SOURCE_CAMERA_BASEANGLEZ) == L"0" &&
             camera->ParGet(PAR_SOURCE_CAMERA_MOVESPEED) == L"0");
    auto *strategy = camera->BlockGet(PAR_SOURCE_CAMERA_STRATEGY);
    for (const auto &[key, value] : {std::pair{PAR_SOURCE_CAMERA_DISTMIN, L"70"}, {PAR_SOURCE_CAMERA_DISTMAX, L"70"},
                                   {PAR_SOURCE_CAMERA_ROTANGLEMIN, L"45"}, {PAR_SOURCE_CAMERA_ROTANGLEMAX, L"45"},
                                   {PAR_SOURCE_CAMERA_HEIGHT, L"70"}, {PAR_SOURCE_CAMERA_ROTSPEEDX, L"0"},
                                   {PAR_SOURCE_CAMERA_ROTSPEEDZ, L"0"}, {PAR_SOURCE_CAMERA_WHEELSTEP, L"0"}})
        MG_CHECK(strategy->ParGet(key) == value);
    auto *body = data.BlockGet(L"Models")->BlockGet(L"Flyers")->BlockGet(L"1")->BlockGet(L"Body");
    MG_CHECK(body->ParGet(L"Model") == L"synthetic-calcvis-flyer.vo" && body->ParGet(L"Texture") == L"pixel");
}

void validate_native_calcvis(const std::filesystem::path &path, bool ridge) {
    CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    auto *names = map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR);
    auto *values = map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR);
    for (const auto &[key, value] : {std::pair{DATA_SIZEX, L"32"}, {DATA_SIZEY, L"32"},
                                   {DATA_CAMPOSX, L"120"}, {DATA_CAMPOSY, L"320"}, {DATA_CAMANGLE, L"0"}}) {
        const int index = names->FindAsWStr(key);
        MG_CHECK(index >= 0 && values->GetAsWStr(index) == value);
    }
    auto *buffer = map.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE);
    MG_CHECK(buffer && buffer->GetArraysCount() == 1 && buffer->GetArrayLength(0) == 33 * 33 * sizeof(SCompilePoint));
    for (int y = 0; y <= size; ++y)
        for (int x = 0; x <= size; ++x) {
            SCompilePoint point{};
            std::memcpy(&point, buffer->GetFirst<BYTE>(0) + (x + y * 33) * sizeof(point), sizeof(point));
            MG_CHECK((point.flags & CELLFLAG_LAND) != 0 && std::isfinite(point.z));
            MG_CHECK(point.z == height(x, ridge));
        }
    auto *groups = map.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
    const int side = (size + MAP_GROUP_SIZE - 1) / MAP_GROUP_SIZE;
    MG_CHECK(groups && groups->GetArraysCount() == unsigned(side * side));
    for (int y = 0, group = 0; y < size; y += MAP_GROUP_SIZE)
        for (int x = 0; x < size; x += MAP_GROUP_SIZE, ++group) {
            const int width = std::min(MAP_GROUP_SIZE, size - x), height = std::min(MAP_GROUP_SIZE, size - y);
            const auto length = groups->GetArrayLength(group);
            const auto *raw = groups->GetFirst<BYTE>(group);
            size_t offset = 0;
            const auto read = [&]<class T>() {
                MG_CHECK(offset <= length && sizeof(T) <= length - offset);
                T result{};
                std::memcpy(&result, raw + offset, sizeof(T));
                offset += sizeof(T);
                return result;
            };
            MG_CHECK(read.operator()<WORD>() == x && read.operator()<WORD>() == y);
            MG_CHECK(read.operator()<DWORD>() == 1 && read.operator()<DWORD>() == 0);
            MG_CHECK(read.operator()<DWORD>() == unsigned(width * height * 6 * sizeof(WORD)));
            for (int row = 0; row < height; ++row)
                for (int column = 0; column < width; ++column) {
                    const WORD a = WORD(column + row * (width + 1)), b = WORD(a + 1),
                               c = WORD(a + width + 1), d = WORD(c + 1);
                    for (WORD expected : {a, b, c, b, d, c}) MG_CHECK(read.operator()<WORD>() == expected);
                }
            MG_CHECK(read.operator()<DWORD>() == unsigned((width + 1) * (height + 1) * sizeof(SCompileBottomVert)));
            for (int row = 0; row <= height; ++row)
                for (int column = 0; column <= width; ++column) {
                    const auto vertex = read.operator()<SCompileBottomVert>();
                    MG_CHECK(vertex.x == x + column && vertex.y == y + row &&
                             vertex.tx == column * 16 && vertex.ty == row * 16);
                }
            MG_CHECK(offset == length);
        }
}

void check_calcvis_runtime(const CMatrixMap &map) {
    MG_CHECK(map.m_Size.x == size && map.m_Size.y == size && map.m_Point && map.m_Unit && map.m_Group);
    const int side = (size + MAP_GROUP_SIZE - 1) / MAP_GROUP_SIZE;
    MG_CHECK(map.m_GroupSize.x == side && map.m_GroupSize.y == side);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            MG_CHECK(map.m_Unit[x + y * size].IsLand());
    for (int i = 0; i < 33 * 33; ++i)
        MG_CHECK(std::isfinite(map.m_Point[i].z) && std::isfinite(map.m_Point[i].z_land));
    for (int i = 0; i < side * side; ++i) MG_CHECK(map.m_Group[i]);
    const auto center = map.m_Camera.GetFrustumCenter();
    MG_CHECK(std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z));
    // Require the real coordinate interval before truncation; negative fractions are unsafe too.
    MG_CHECK(center.x >= 0 && center.y >= 0 && center.x < size * GLOBAL_SCALE && center.y < size * GLOBAL_SCALE);
    const int gx = int(center.x / (MAP_GROUP_SIZE * GLOBAL_SCALE)),
              gy = int(center.y / (MAP_GROUP_SIZE * GLOBAL_SCALE));
    MG_CHECK(gx >= 0 && gy >= 0 && gx < side && gy < side);
}
}
