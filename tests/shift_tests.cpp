// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "battle_scenario.hpp"
#include "battle_material.hpp"
#include "MatrixRobot.hpp"
#include "MatrixMap.hpp"
#include "MatrixMapGroup.hpp"
#include "MatrixRenderPipeline.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define MATRIX_SHIFT_ASAN 1
#endif

namespace {
using tests::battle::Session;
constexpr std::array keys{0, 10, 20, 30, 40, 15, 25, 15, -10, 50, 60, 70};

void object_insertion() {
    Session session;
    CMatrixMapStatic::SortBegin();
    std::vector<std::pair<int, CMatrixMapStatic *>> expected;
    for (int key : keys) {
        auto *object = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 320, 320);
        D3DXMATRIX matrix;
        D3DXMatrixIdentity(&matrix);
        matrix._33 = 0;
        matrix._43 = float(key) / 256;
        object->Sort(matrix);
        expected.emplace_back(key, object);
        std::stable_sort(expected.begin(), expected.end(), [](auto a, auto b) { return a.first < b.first; });
        MG_CHECK(CMatrixMapStatic::GetVisObjCnt() == int(expected.size()));
        for (unsigned i = 0; i < expected.size(); ++i) {
            MG_CHECK(CMatrixMapStatic::GetVisObj(int(i)) == expected[i].second);
        }
        object->Sort(matrix); // The same-frame guard must not duplicate entries.
        MG_CHECK(CMatrixMapStatic::GetVisObjCnt() == int(expected.size()));
    }
}

template<class T> void append(std::vector<BYTE> &bytes, T value) {
    const auto *begin = reinterpret_cast<const BYTE *>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

void surface_insertion() {
    Session session;
    // Three texture stages avoid querying a native adapter in pipeline setup.
    struct Pipeline {
        DWORD stages = g_D3DDCaps.MaxSimultaneousTextures;
        CRenderPipeline *previous = g_Render;
        std::unique_ptr<CRenderPipeline> owned;
        Pipeline() {
            g_D3DDCaps.MaxSimultaneousTextures = 3;
            owned = std::make_unique<CRenderPipeline>();
            g_Render = owned.get();
            for (int i = 0; i < SURF_TYPES_COUNT; ++i) {
                g_Render->m_TerSurfPass[i] = 0;
                g_Render->m_TerSurfClear[i] = [] {};
            }
        }
        ~Pipeline() { g_Render = previous; g_D3DDCaps.MaxSimultaneousTextures = stages; }
    } pipeline;
    tests::install_battle_texture(g_MatrixMap->IdsGet(0).c_str());
    CTerSurface::AllocSurfaces(int(keys.size()));
    for (unsigned i = 0; i < keys.size(); ++i) {
        std::vector<BYTE> bytes;
        append(bytes, int{0}); append(bytes, keys[i]); append(bytes, DWORD(0xff000000 | i));
        append(bytes, DWORD{3}); append(bytes, DWORD{6}); append(bytes, DWORD{1});
        append(bytes, 0.f); append(bytes, 0.f);
        for (int vertex = 0; vertex < 3; ++vertex) {
            append(bytes, float(100 + vertex)); append(bytes, 100.f); append(bytes, 0.f);
            append(bytes, DWORD{0xffffffff}); append(bytes, 0.f); append(bytes, 0.f);
        }
        for (WORD index : {WORD{0}, WORD{1}, WORD{2}}) append(bytes, index);
        append(bytes, DWORD{0}); // All surfaces are enumerated in load order by group 0.
        CTerSurface::Load(int(i), bytes.data());
    }
    std::vector<DWORD> actual;
    session.surface_observer([&](DWORD color) { actual.push_back(color); });
    CTerSurface::BeforeDrawAll();
    g_MatrixMap->GetGroupByIndex(0)->BeforeDrawSurfaces();
    g_MatrixMap->GetGroupByIndex(0)->BeforeDrawSurfaces(); // Frame guard also covers group revisits.
    CTerSurface::DrawAll(); // Zero passes: observe traversal, never submit a primitive.
    std::vector<unsigned> expected(keys.size());
    for (unsigned i = 0; i < keys.size(); ++i) expected[i] = i;
    std::stable_sort(expected.begin(), expected.end(), [](unsigned a, unsigned b) { return keys[a] < keys[b]; });
    MG_CHECK(actual.size() == expected.size());
    for (unsigned i = 0; i < expected.size(); ++i)
        MG_CHECK(actual[i] == (0xff000000 | expected[i]));
    session.surface_observer({});
}

void order_rotation() {
    Session session;
    auto *actor = CMatrixMapStatic::GetFirstLogic()->AsRobot();
    MG_CHECK(actor && actor->GetOrdersInPool() == 0);
    const auto release = [](SObjectCore *core) { core->Release(); };
    std::unique_ptr<SObjectCore, decltype(release)> target(actor->GetCore(DEBUG_CALL_INFO), release);
    const int initial_refs = target->m_Ref;
    const auto draws = random::draw_count();
    std::vector<SOrder> expected;
    // The complete object is alive. Snapshot the adjacent object representation;
    // ASan additionally makes the old full-pool overread observable.
    auto *adjacent = reinterpret_cast<unsigned char *>(actor->GetOrder(0)) + sizeof(SOrder) * MAX_ORDERS;
    static_assert(alignof(SCaptureCandidate) <= alignof(SOrder));
    std::array<unsigned char, sizeof(SCaptureCandidate) * MAX_CAPTURE_CANDIDATES + sizeof(int)> saved;
    std::memcpy(saved.data(), adjacent, saved.size());
    const auto check = [&] {
        MG_CHECK(actor->GetOrdersInPool() == int(expected.size()));
        for (unsigned i = 0; i < expected.size(); ++i) {
            const auto &a = *actor->GetOrder(int(i));
            const auto &b = expected[i];
            MG_CHECK(a.m_OrderType == b.m_OrderType && a.m_OrderPhase == b.m_OrderPhase);
            MG_CHECK(std::bit_cast<unsigned>(a.m_Param1) == std::bit_cast<unsigned>(b.m_Param1));
            MG_CHECK(std::bit_cast<unsigned>(a.m_Param2) == std::bit_cast<unsigned>(b.m_Param2));
            MG_CHECK(std::bit_cast<unsigned>(a.m_Param3) == std::bit_cast<unsigned>(b.m_Param3));
            MG_CHECK(a.m_Param4 == b.m_Param4 && a.m_ObjCore == b.m_ObjCore);
        }
        MG_CHECK(target->m_Ref == initial_refs + int(expected.size()));
        MG_CHECK(std::memcmp(saved.data(), adjacent, saved.size()) == 0);
        MG_CHECK(session.frame() == 0 && session.time() == 0 && random::draw_count() == draws);
    };
    for (int count = 0; count <= MAX_ORDERS; ++count) {
        if (count > 0) {
            auto *order = actor->AllocPlaceForOrderOnTop();
            MG_CHECK(order);
            order->SetOrder(ROT_FIRE, actor);
            order->SetPhase(static_cast<OrderPhase>(count));
            order->m_Param1 = float(count); order->m_Param2 = -float(count);
            order->m_Param3 = count == 1 ? -0.f : count * .25f; order->m_Param4 = 100 + count;
            expected.insert(expected.begin(), *order);
        }
        for (int rotation = 0; rotation <= count; ++rotation) {
#ifdef MATRIX_SHIFT_ASAN
            __asan_poison_memory_region(adjacent, saved.size());
#endif
            actor->ProcessOrdersList();
#ifdef MATRIX_SHIFT_ASAN
            __asan_unpoison_memory_region(adjacent, saved.size());
#endif
            if (!expected.empty()) std::rotate(expected.begin(), expected.begin() + 1, expected.end());
            check();
        }
    }
    while (actor->GetOrdersInPool()) actor->RemoveOrder(0);
    expected.clear();
    check();
#ifdef MATRIX_SHIFT_ASAN
    std::cout << "ASan adjacent-storage read guard enabled\n";
#endif
}

std::array<D3DXVECTOR3, 9> points() {
    std::array<D3DXVECTOR3, 9> result;
    for (unsigned i = 0; i < result.size(); ++i) result[i] = D3DXVECTOR3(float(i) * 10, float(i * i), float(i % 3));
    return result;
}
void check_point(D3DXVECTOR3 a, D3DXVECTOR3 b) {
    MG_CHECK(std::abs(a.x - b.x) < .0002f && std::abs(a.y - b.y) < .0002f && std::abs(a.z - b.z) < .0002f);
}
void trajectory_trim() {
    const auto input = points();
    for (bool approximate : {false, true}) {
        CTrajectory original(nullptr), trimmed(nullptr);
        if (approximate) { original.Init1(input.data(), int(input.size())); trimmed.Init1(input.data(), int(input.size())); }
        else { original.Init2(input.data(), int(input.size())); trimmed.Init2(input.data(), int(input.size())); }
        // Both constructors produce eight segments. Remove one, retaining seven overlapping records.
        MG_CHECK(trimmed.CutBefore(.1875f) == .5f);
        for (int i = 0; i <= 32; ++i) {
            D3DXVECTOR3 a, b;
            const float t = float(i) / 32;
            original.CalcPoint(a, (1 + t * 7) / 8); trimmed.CalcPoint(b, t); check_point(a, b);
        }
    }
}
void trajectory_current() {
    const auto input = points();
    CTrajectory original(nullptr), trimmed(nullptr);
    original.Init2(input.data(), int(input.size())); trimmed.Init2(input.data(), int(input.size()));
    trimmed.CutBeforeCurrent(); // No current segment to discard yet.
    original.Move(15); trimmed.Move(15);
    D3DXVECTOR3 before, after;
    original.GetCurPos(before); trimmed.CutBeforeCurrent(); trimmed.GetCurPos(after); check_point(before, after);
    D3DXVECTOR3 end;
    original.CalcPoint(end, 1);
    trimmed.CalcPoint(after, 1); check_point(end, after);
    // Continue within and across surviving segments; distance and interpolation stay identical.
    for (int i = 0; i < 8; ++i) {
        original.Move(3); trimmed.Move(3);
        original.GetCurPos(before); trimmed.GetCurPos(after); check_point(before, after);
    }
    trimmed.CutBeforeCurrent(); trimmed.GetCurPos(after); check_point(before, after);
    trimmed.CalcPoint(after, 1); check_point(end, after);
}
void trajectory_boundaries() {
    const auto input = points();
    CTrajectory original(nullptr), trimmed(nullptr);
    original.Init2(input.data(), int(input.size())); trimmed.Init2(input.data(), int(input.size()));
    MG_CHECK(trimmed.CutBefore(-.25f) == -.25f);
    MG_CHECK(trimmed.CutBefore(0) == 0);
    D3DXVECTOR3 a, b;
    original.CalcPoint(a, .5f); trimmed.CalcPoint(b, .5f); check_point(a, b);
    MG_CHECK(trimmed.CutBefore(.875f) == 0); // Retain exactly one segment.
    MG_CHECK(trimmed.CutBefore(.5f) == .5f);
    original.CalcPoint(a, .9375f); trimmed.CalcPoint(b, .5f); check_point(a, b);
    // Legacy endpoint behavior retains the FIRST segment. This independent bug
    // is characterized, not corrected by this overlapping-copy change.
    trimmed.Init2(input.data(), int(input.size()));
    MG_CHECK(trimmed.CutBefore(1) == 1);
    original.CalcPoint(a, .0625f); trimmed.CalcPoint(b, .5f); check_point(a, b);
}
constexpr tests::Case cases[] = {
    {"game.memory.object_insertion", object_insertion}, {"game.memory.surface_insertion", surface_insertion},
    {"game.memory.order_rotation", order_rotation}, {"game.memory.trajectory_trim", trajectory_trim},
    {"game.memory.trajectory_current", trajectory_current}, {"game.memory.trajectory_boundaries", trajectory_boundaries},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
