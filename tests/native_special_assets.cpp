// MatrixGame - licensed under GPLv2 or any later version.
#include "native_special_assets.hpp"
#include "native_session_assets.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "CStorage.hpp"
#include <array>
#include <filesystem>

namespace tests {
namespace {
void schema(CStorage &storage, const wchar_t *name,
            std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns) {
    CStorageRecord record(name);
    for (const auto &[column, type] : columns) record.AddItem(CStorageRecordItem(column, type));
    storage.AddRecord(record);
    for (const auto &[column, type] : columns)
        if (type != ST_WCHAR) storage.GetBuf(name, column, type)->AddArray();
}
template<class T> void set(CStorage &storage, const wchar_t *table, const wchar_t *column,
                           EStorageType type, std::initializer_list<T> values) {
    storage.GetBuf(table, column, type)->SetArray<T>(0, values.begin(), static_cast<int>(values.size()));
}
std::wstring descriptor(const wchar_t *texture, const std::wstring &behavior) {
    std::array<std::wstring, OTP_COUNT> fields{};
    fields[OTP_PATH] = L"Matrix/Fixture/";
    fields[OTP_VO] = L"special";
    fields[OTP_TEXTURE] = texture;
    fields[OTP_TEXTURE_SCROLL] = L"0,0";
    fields[OTP_SHADOW] = L"Off";
    fields[OTP_BEHAVIOUR] = behavior;
    fields[OTP_BIAS] = fields[OTP_INVLOGIC] = L"0";
    std::wstring result;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) result += L"*";
        result += fields[i];
    }
    return result;
}
void image(const wchar_t *name, DWORD color) {
    CBitmap bitmap;
    bitmap.CreateRGBA(32, 32);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
        reinterpret_cast<DWORD *>(static_cast<BYTE *>(bitmap.Data()) + y * bitmap.Pitch())[x] = color;
    bitmap.SaveInPNG((std::wstring(L"Matrix/Fixture/") + name + L".png").c_str());
}
void special_mesh() {
    const auto path = L"Matrix/Fixture/special.vo";
    write_battle_mesh(path, {}, false, L"Matrix/Fixture/grey");
    CStorage mesh;
    MG_CHECK(mesh.Load(path));
    auto *names = mesh.GetBuf(L"anims", L"name", ST_WCHAR);
    for (const auto *name : {L"Main", L"Pain", L"Death"}) {
        const DWORD id = names->GetArraysCount();
        const DWORD offset = mesh.GetBuf(L"anims", L"frames", ST_BYTE)->GetArrayLength(0) / sizeof(SVOFrameIndex);
        names->AddWStr(name);
        mesh.GetBuf(L"anims", L"id", ST_DWORD)->AddToArray<DWORD>(0, id);
        mesh.GetBuf(L"anims", L"disp", ST_DWORD)->AddToArray<DWORD>(0, offset);
        mesh.GetBuf(L"anims", L"cnt", ST_DWORD)->AddToArray<DWORD>(0, 1);
        const SVOFrameIndex frame{0, 100};
        mesh.GetBuf(L"anims", L"frames", ST_BYTE)->AddToArray<BYTE>(0,
            reinterpret_cast<const BYTE *>(&frame), sizeof(frame));
    }
    mesh.Save(path);
}
void scene(bool terron) {
    const auto path = terron ? L"Matrix/Map/mixed-boss-break.cmap" : L"Matrix/Map/mixed-break-anim.cmap";
    write_native_session_map(path);
    CStorage map;
    MG_CHECK(map.Load(path));
    auto *strings = map.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, ST_WCHAR);
    MG_CHECK(strings->GetArraysCount() == 1); // Terrain texture retains index zero.
    strings->AddWStr(descriptor(L"grey", L"Static"));
    strings->AddWStr(descriptor(L"red", terron ? L"+Break,1,300,Terron" : L"+Break,1,300,None"));
    strings->AddWStr(descriptor(L"green", terron ? L"+Break,1,300,None" : L"+Anim,0:0:300:1#1:1:0:-1"));
    strings->AddWStr(descriptor(L"blue", L"Break,1,300,None"));
    map.DelRecord(DATA_OBJECTS);
    schema(map, DATA_OBJECTS, {{DATA_OBJECTS_X, ST_FLOAT}, {DATA_OBJECTS_Y, ST_FLOAT},
        {DATA_OBJECTS_Z, ST_FLOAT}, {DATA_OBJECTS_ANGLE_Z, ST_FLOAT}, {DATA_OBJECTS_SCALE, ST_FLOAT},
        {DATA_OBJECTS_TYPE, ST_DWORD}});
    set<float>(map, DATA_OBJECTS, DATA_OBJECTS_X, ST_FLOAT, terron ?
        std::initializer_list<float>{100, 320, 540, 100} : std::initializer_list<float>{100, 220, 420, 320});
    set<float>(map, DATA_OBJECTS, DATA_OBJECTS_Y, ST_FLOAT, terron ?
        std::initializer_list<float>{540, 320, 100, 320} : std::initializer_list<float>{540, 300, 300, 440});
    set<float>(map, DATA_OBJECTS, DATA_OBJECTS_Z, ST_FLOAT, {10, 10, 10, 10});
    set<float>(map, DATA_OBJECTS, DATA_OBJECTS_ANGLE_Z, ST_FLOAT, {0, 0, 0, 0});
    set<float>(map, DATA_OBJECTS, DATA_OBJECTS_SCALE, ST_FLOAT, terron ?
        std::initializer_list<float>{1, 3, 1, 1} : std::initializer_list<float>{1, 1, 1, 1});
    set<DWORD>(map, DATA_OBJECTS, DATA_OBJECTS_TYPE, ST_DWORD, {1, 2, 3, 4});

    // All robots are friendly: idle collateral/control units cannot wander under enemy AI.
    set<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE, {1, 1});
    set<float>(map, DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT, {320, 500});
    set<float>(map, DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT, {100, 500});
    auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR);
    const auto unarmed = units->GetAsWStr(0);
    units->SetWStr(0, unarmed + L"|" + std::to_wstring(MRT_WEAPON) + L"," + std::to_wstring(RUK_WEAPON_LASER));
    if (terron) {
        // Near collateral victim and distant control, separate from the shooter.
        set<float>(map, DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT, {420, 560, 260});
        set<float>(map, DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT, {160, 560, 320});
        set<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE, {1, 1, 1});
        set<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE, {0, 0, 0});
        set<INT32>(map, DATA_ROBOTS, DATA_ROBOTS_SHADOWSIZE, ST_INT32, {0, 0, 0});
        set<INT32>(map, DATA_ROBOTS, DATA_ROBOTS_GROUP, ST_INT32, {0, 0, 0});
        units->AddWStr(unarmed);
    }
    for (const auto *name : {DATA_CAMPOSX, DATA_CAMPOSY}) {
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(name);
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"320");
    }
    map.Save(path);
}
}
void write_native_special_scenes() {
    MG_CHECK(!g_MatrixMap && !g_Cache && !g_MatrixHeap);
    MG_CHECK(std::filesystem::exists(L"pixel.png") && std::filesystem::exists(L"CFG/robots/data.txt"));
    std::filesystem::create_directories(L"Matrix/Map");
    std::filesystem::create_directories(L"Matrix/Fixture");
    image(L"grey", 0xff808080); image(L"red", 0xff4040e0);
    image(L"green", 0xff40c040); image(L"blue", 0xffe08040);
    special_mesh();
    scene(false); scene(true);
}
}
