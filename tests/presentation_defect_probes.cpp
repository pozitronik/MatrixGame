// MatrixGame - licensed under GPLv2 or any later version.
// Opt-in correctness probes: known failures are not approved baseline goldens.
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

void clear_rectangle() {
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
        element->SetState(IFACE_NORMAL);
        element->m_StateImages[IFACE_NORMAL].pImage = &texture;
        element->m_StateImages[IFACE_NORMAL].m_Geom[1].p.x = 100;
        element->m_StateImages[IFACE_NORMAL].m_Geom[1].p.y = 200;
        element->m_ClearRect = CRect(3, 11, 20, 30);
        element->SetClearRect();
        CInterface::ClearRects_Clear();
        element->BeforeRender();
        MG_CHECK(native.preloads == 1 && CInterface::ClearRects_GetCount() == 1);
        actual = CInterface::ClearRects_Get()[0];
    }
    MG_CHECK(native.references == 0);
    std::cout << "ClearRect actual=" << actual.x1 << ',' << actual.y1 << ',' << actual.x2 << ',' << actual.y2
              << " expected=103,211,120,230\n";
    MG_CHECK(actual.x1 == 103 && actual.x2 == 120 && actual.y2 == 230);
    MG_CHECK(actual.y1 == 211); // Top offset, not the left offset.
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
