// MatrixGame - licensed under GPLv2 or any later version.
#include "native_outcome_assets.hpp"
#include "native_session_assets.hpp"
#include "test_support.hpp"
#include "CStorage.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"

namespace tests {
void write_native_outcome_map(const std::filesystem::path &path, bool player_wins) {
    write_native_session_map(path);
    Base::CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    auto *x = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_X, Base::ST_FLOAT);
    auto *y = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_Y, Base::ST_FLOAT);
    MG_CHECK(x && y);
    MG_CHECK(x->GetArrayLength(0) == 2 && y->GetArrayLength(0) == 2);
    x->GetFirst<float>(0)[0] = 250;
    x->GetFirst<float>(0)[1] = 350;
    y->GetFirst<float>(0)[0] = y->GetFirst<float>(0)[1] = 250;
    auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, Base::ST_WCHAR);
    const int armed = player_wins ? 0 : 1;
    MG_CHECK(units && units->GetArraysCount() == 2);
    const auto modules = units->GetAsWStr(armed) + L"|" + std::to_wstring(MRT_WEAPON) + L"," + std::to_wstring(RUK_WEAPON_LASER);
    units->SetWStr(armed, modules);
    map.Save(path.c_str());
}
void remove_native_map_width(const std::filesystem::path &path) {
    Base::CStorage map;
    MG_CHECK(map.Load(path.c_str()));
    auto *names = map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, Base::ST_WCHAR);
    MG_CHECK(names);
    const int width = names->FindAsWStr(DATA_SIZEX);
    MG_CHECK(width >= 0);
    names->SetWStr(width, L"SyntheticMissingWidth");
    map.Save(path.c_str());
}
}
