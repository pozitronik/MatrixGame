// MatrixGame - licensed under GPLv2 or any later version.
// Clear-rectangle regressions and opt-in, unresolved plane correctness probes.
#include "test_support.hpp"
#include "command_world.hpp"
#include "Interface/CIFaceElement.h"
#include "Interface/CInterface.h"
#include "Math3D.hpp"
#include <cmath>

namespace {
// Only residency notification is needed by BeforeRender. All other resource
// operations fail loudly instead of pretending to upload or render a texture.
class PreloadedTexture final : public IDirect3DTexture9 {
    [[noreturn]] static void unsupported() { std::abort(); }
public:
    ULONG references = 1;
    unsigned preloads = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **out) override {
        if (out) *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { MG_CHECK(references > 0); return --references; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { unsupported(); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { unsupported(); }
    DWORD STDMETHODCALLTYPE GetPriority() override { unsupported(); }
    void STDMETHODCALLTYPE PreLoad() override { ++preloads; }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { unsupported(); }
    DWORD STDMETHODCALLTYPE GetLOD() override { unsupported(); }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { unsupported(); }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { unsupported(); }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { unsupported(); }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override { unsupported(); }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT, D3DSURFACE_DESC *) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT, IDirect3DSurface9 **) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE LockRect(UINT, D3DLOCKED_RECT *, const RECT *, DWORD) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT) override { unsupported(); }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *) override { unsupported(); }
};

D3DRECT clear_rectangle_at(float x, float y, const CRect &rectangle, IFaceElementState state, bool enabled) {
    tests::CommandWorld world;
    struct InterfaceScope {
        CIFaceList *previous = g_IFaceList;
        tests::EnginePtr<CIFaceList> ui = tests::make_engine_object<CIFaceList>();
        InterfaceScope() { g_IFaceList = ui.get(); }
        ~InterfaceScope() { ui.reset(); g_IFaceList = previous; }
    } ui;
    PreloadedTexture native;
    D3DRECT actual{};
    {
        CTextureManaged texture;
        texture.Set(&native); // Production wrapper owns the single reference.
        auto element = tests::make_engine_object<CIFaceElement>();
        element->SetState(state);
        // The active image geometry, not the element origin or another state, is authoritative.
        element->m_xPos = 900;
        element->m_yPos = 700;
        for (int i = 0; i < MAX_STATES; ++i) {
            element->m_StateImages[i].pImage = &texture;
            element->m_StateImages[i].m_Geom[1].p.x = 600;
            element->m_StateImages[i].m_Geom[1].p.y = 400;
        }
        element->m_StateImages[state].m_Geom[1].p.x = x;
        element->m_StateImages[state].m_Geom[1].p.y = y;
        element->m_ClearRect = rectangle;
        MG_CHECK(!element->HasClearRect());
        if (enabled) element->SetClearRect();
        CInterface::ClearRects_Clear();
        element->BeforeRender();
        MG_CHECK(native.preloads == 1);
        MG_CHECK(CInterface::ClearRects_GetCount() == (enabled ? 1 : 0));
        if (enabled) actual = CInterface::ClearRects_Get()[0];
        CInterface::ClearRects_Clear();
        MG_CHECK(CInterface::ClearRects_GetCount() == 0);
    }
    MG_CHECK(native.references == 0);
    return actual;
}
void check_rectangle(const D3DRECT &actual, const D3DRECT &expected) {
    MG_CHECK(actual.x1 == expected.x1 && actual.x2 == expected.x2 && actual.y2 == expected.y2);
    MG_CHECK(actual.y1 == expected.y1);
}
void clear_rectangle() {
    const auto actual = clear_rectangle_at(100, 200, CRect(3, 11, 20, 30), IFACE_NORMAL, true);
    std::cout << "ClearRect actual=" << actual.x1 << ',' << actual.y1 << ',' << actual.x2 << ',' << actual.y2
              << " expected=103,211,120,230\n";
    check_rectangle(actual, {103, 211, 120, 230});
}
void clear_rectangle_states() {
    struct Sample { float x, y; CRect rectangle; D3DRECT expected; };
    const Sample samples[]{
        {100, 200, {3, 11, 20, 30}, {103, 211, 120, 230}},
        {640, 480, {11, 3, 20, 30}, {651, 483, 660, 510}},
        {800, 600, {-20, -40, 0, 0}, {780, 560, 800, 600}},
        {-10, 50, {20, -10, 40, 10}, {10, 40, 30, 60}},
        {320, 240, {0, 0, 40, 80}, {320, 240, 360, 320}},
        {100, 200, {7, 7, 17, 27}, {107, 207, 117, 227}},
    };
    for (int state = 0; state < MAX_STATES; ++state)
        for (const auto &sample : samples)
            check_rectangle(clear_rectangle_at(sample.x, sample.y, sample.rectangle,
                static_cast<IFaceElementState>(state), true), sample.expected);
}
void clear_rectangle_disabled() {
    for (int state = 0; state < MAX_STATES; ++state)
        check_rectangle(clear_rectangle_at(100, 200, {3, 11, 20, 30},
            static_cast<IFaceElementState>(state), false), {});
}

void plane_intersection(bool points) {
    SPlane plane{};
    // D3DX vector default construction does not initialize components. Keep
    // the separate fields read by Distance/FindIntersect deterministic.
    plane.norm = D3DXVECTOR3(0, 0, 0);
    plane.dist = 0;
    if (points) SPlane::BuildFromPoints(plane, {0, 0, 2}, {1, 0, 2}, {0, 1, 2});
    else SPlane::BuildFromPointNormal(plane, {0, 0, 2}, {0, 0, 1});
    MG_CHECK(plane.dxplane.a == 0 && plane.dxplane.b == 0);
    MG_CHECK(std::abs(plane.dxplane.c - 1) < 0.0001f);
    MG_CHECK(std::abs(plane.dxplane.d + 2) < 0.0001f);
    const float signed_distance = plane.Distance({0, 0, 10});
    float intersection = -1;
    const bool hit = plane.FindIntersect({0, 0, 10}, {0, 0, -1}, intersection);
    std::cout << "Plane distance=" << signed_distance << " hit=" << hit
              << " intersection=" << intersection << " expected magnitude=8, hit=1, intersection=8\n";
    MG_CHECK(std::abs(std::abs(signed_distance) - 8) < 0.0001f);
    MG_CHECK(hit && std::abs(intersection - 8) < 0.0001f);
}
void plane_normal() { plane_intersection(false); }
void plane_points() { plane_intersection(true); }

const tests::Case cases[] = {
    {"probe.presentation.clear_rectangle", clear_rectangle},
    {"game.presentation.clear_rectangle", clear_rectangle},
    {"game.presentation.clear_rectangle_states", clear_rectangle_states},
    {"game.presentation.clear_rectangle_disabled", clear_rectangle_disabled},
    {"probe.presentation.plane_normal", plane_normal},
    {"probe.presentation.plane_points", plane_points},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
