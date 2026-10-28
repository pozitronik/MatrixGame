// MatrixGame - licensed under GPLv2 or any later version.
#include "native_calcvis_assets.hpp"
#include "native_session_assets.hpp"
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "CStorage.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstring>
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;
namespace {
struct Directory {
    fs::path previous = fs::current_path(), root = previous / ("calcvis-" + std::to_string(GetCurrentProcessId()));
    Directory() {
        MG_CHECK(!fs::exists(root) && fs::create_directory(root));
        fs::current_path(root);
    }
    ~Directory() {
        std::error_code error;
        fs::current_path(previous, error);
        if (!error) fs::remove_all(root, error);
        if (error) std::cerr << "Cannot clean CalcVis fixture: " << error.message() << '\n';
    }
};

std::string bytes(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    MG_CHECK(f.good());
    return {std::istreambuf_iterator<char>(f), {}};
}

template<class F>
void refused(F &&action) {
    bool failed = false;
    try {
        action();
    }
    catch (const std::runtime_error &) {
        failed = true;
    }
    MG_CHECK(failed);
}

tests::battle::Profile profile(random::Mode mode) {
    tests::battle::Profile p;
    p.audio = false;
    p.initially_selected = false;
    p.layout = tests::NavigationLayout::Open;
    p.mode = mode;
    p.seed = 87;
    return p;
}

void runtime_guard() {
    tests::battle::Session session(profile(random::Mode::ParkMiller));
    auto &map = *g_MatrixMap;
    tests::check_calcvis_runtime(map);
    for (int index : {0, 31, 32, 32 * 32 - 1}) {
        map.m_Unit[index].SetType(CELLFLAG_WATER);
        refused([&] {
            tests::check_calcvis_runtime(map);
        });
        map.m_Unit[index].SetType(CELLFLAG_LAND | CELLFLAG_FLAT);
    }
    map.m_Point[33 * 33 - 1].z_land = std::numeric_limits<float>::quiet_NaN();
    refused([&] {
        tests::check_calcvis_runtime(map);
    });
    map.m_Point[33 * 33 - 1].z_land = 10;
    auto *group = map.m_Group[0];
    map.m_Group[0] = nullptr;
    refused([&] {
        tests::check_calcvis_runtime(map);
    });
    map.m_Group[0] = group;
    for (auto point : {D3DXVECTOR2(-1000, -1000), D3DXVECTOR2(1000, 1000)}) {
        map.m_Camera.SetXYStrategy(point);
        map.m_Camera.Takt(0);
        map.m_Camera.BeforeDraw();
        refused([&] {
            tests::check_calcvis_runtime(map);
        });
    }
    map.m_Camera.SetXYStrategy(D3DXVECTOR2(320, 320));
    map.m_Camera.Takt(0);
    map.m_Camera.BeforeDraw();
    tests::check_calcvis_runtime(map);
}

void input_guard() {
    Directory directory;
    tests::write_native_calcvis_map(L"flat.cmap", false);
    tests::validate_native_calcvis(L"flat.cmap", false);
    CStorage map;
    MG_CHECK(map.Load(L"flat.cmap"));
    auto *buffer = map.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE);
    SCompilePoint point{};
    std::memcpy(&point, buffer->GetFirst<BYTE>(0), sizeof(point));
    point.flags = CELLFLAG_WATER;
    std::memcpy(buffer->GetFirst<BYTE>(0), &point, sizeof(point));
    map.Save(L"water.cmap");
    refused([&] {
        tests::validate_native_calcvis(L"water.cmap", false);
    });
    point.flags = CELLFLAG_LAND;
    point.z = std::numeric_limits<float>::infinity();
    std::memcpy(buffer->GetFirst<BYTE>(0), &point, sizeof(point));
    map.Save(L"nonfinite.cmap");
    refused([&] {
        tests::validate_native_calcvis(L"nonfinite.cmap", false);
    });
    tests::write_native_calcvis_map(L"ridge.cmap", true);
    tests::validate_native_calcvis(L"ridge.cmap", true);
    refused([&] {
        tests::validate_native_calcvis(L"ridge.cmap", false);
    });
    for (int mutation = 0; mutation < 3; ++mutation) {
        CStorage bad;
        MG_CHECK(bad.Load(L"ridge.cmap"));
        auto *groups = bad.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
        if (mutation == 0) {
            const WORD wrong = 65535;
            std::memcpy(groups->GetFirst<BYTE>(0), &wrong, sizeof(wrong));
        }
        else if (mutation == 1) {
            const WORD wrong = 65535;
            std::memcpy(groups->GetFirst<BYTE>(0) + 16, &wrong, sizeof(wrong));
        }
        else {
            const BYTE short_record[] = {0, 0, 0};
            groups->SetArray<BYTE>(0, short_record, 3);
        }
        bad.Save(L"bad-group.cmap");
        refused([&] {
            tests::validate_native_calcvis(L"bad-group.cmap", true);
        });
    }
}

void production(bool ridge) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Directory directory;
        tests::write_native_calcvis_map(L"calcvis.cmap", ridge);
        tests::validate_native_calcvis(L"calcvis.cmap", ridge);
        const auto original = bytes(L"calcvis.cmap");
        tests::battle::Session session(profile(mode));
        std::cout << session.metadata("calcvis-all-land-v1") << '\n';
        auto &map = *g_MatrixMap;
        CStorage storage;
        MG_CHECK(storage.Load(L"calcvis.cmap"));
        const auto *buffer = storage.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE)->GetFirst<BYTE>(0);
        for (int i = 0; i < 33 * 33; ++i) {
            SCompilePoint point{};
            std::memcpy(&point, buffer + i * sizeof(point), sizeof(point));
            map.m_Point[i].z = map.m_Point[i].z_land = point.z;
        }
        map.m_Ids[map.m_IdsCnt - 1] = L"calcvis.cmap";
        auto &camera = g_Config.m_CamParams[CAMERA_STRATEGY];
        camera.m_CamDistMin = camera.m_CamDistMax = 70;
        camera.m_CamRotAngleMin = camera.m_CamRotAngleMax = GRAD2RAD(45);
        camera.m_CamHeight = 70;
        map.m_Camera.ResetAngles();
        map.m_Camera.SetXYStrategy(D3DXVECTOR2(120, 320));
        map.m_Camera.Takt(0);
        map.m_Camera.BeforeDraw();
        const auto center = map.m_Camera.GetFrustumCenter();
        std::cout << "Camera=" << center.x << ',' << center.y << ',' << center.z << '\n';
        tests::check_calcvis_runtime(map);
        const auto draws = random::draw_count();
        const int before = map.GetTime();
#if defined(_DEBUG) && !defined(_RELDEBUG)
        auto *sentinel = CHelper::Create(1, 889);
        const auto marker_count = [&] {
            int count = 0;
            for (auto *h = sentinel->Next(); h; h = h->Next()) {
                MG_CHECK(h->Group() == 888);
                ++count;
            }
            return count;
        };
#endif
        map.CalcVis();
#if defined(_DEBUG) && !defined(_RELDEBUG)
        const int count = marker_count();
        MG_CHECK(ridge ? count > 0 : count == 0);
        std::cout << "Debug helper count=" << count << '\n';
#endif
        MG_CHECK(map.m_GroupVis && random::draw_count() == draws && map.GetTime() == before);
        for (int i = 0; i < map.m_GroupSize.x * map.m_GroupSize.y; ++i) {
            const auto &visibility = map.m_GroupVis[i];
            MG_CHECK(visibility.levels_cnt == 0 && visibility.vis_cnt == 0 && visibility.z_from == 0);
        }
        MG_CHECK(bytes(L"calcvis.cmap") == original && !fs::exists(L"calcvis.log"));
        map.CalcVis();
#if defined(_DEBUG) && !defined(_RELDEBUG)
        MG_CHECK(marker_count() == count);
        CHelper::Destroy(sentinel);
#endif
        MG_CHECK(bytes(L"calcvis.cmap") == original && random::draw_count() == draws);
        std::cout << "CalcVis profile=" << (ridge ? "ridge" : "flat") << " rng=" << int(mode)
                  << " map_ticks=0; source unchanged; no saved visibility output\n";
    }
}
}

int main(int argc, char **argv) {
    Base::CMain::BaseInit();
    const tests::Case cases[] = {{"game.presentation.calcvis_runtime_guard", runtime_guard},
                                {"game.presentation.calcvis_input_guard", input_guard},
                                {"game.presentation.calcvis_flat", [] {
                                     production(false);
                                 }},
                                {"game.presentation.calcvis_ridge", [] {
                                     production(true);
                                 }}};
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
