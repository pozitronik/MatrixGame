// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_device.hpp"
#include "test_support.hpp"

#include <cstring>
#include <float.h>
#include <limits>
#include <memory>

extern IDirect3DDevice9 *g_D3DD;

namespace {
template<class T> struct ReleaseCom {
    void operator()(T *object) const { if (object) object->Release(); }
};
template<class T> using ComPtr = std::unique_ptr<T, ReleaseCom<T>>;

ComPtr<IDirect3DVertexBuffer9> vertex_buffer(tests::BattleDevice &device) {
    IDirect3DVertexBuffer9 *raw = nullptr;
    const auto result = device.get()->CreateVertexBuffer(64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        D3DFVF_XYZ, D3DPOOL_DEFAULT, &raw, nullptr);
    ComPtr<IDirect3DVertexBuffer9> owned(raw);
    MG_CHECK(result == D3D_OK && owned);
    return owned;
}

void buffers() {
    tests::BattleDevice device;
    auto vertex = vertex_buffer(device);
    D3DVERTEXBUFFER_DESC description{};
    MG_CHECK(vertex->GetDesc(&description) == D3D_OK);
    MG_CHECK(description.Size == 64 && description.FVF == D3DFVF_XYZ);
    MG_CHECK(description.Usage == (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY));
    MG_CHECK(description.Pool == D3DPOOL_DEFAULT && description.Type == D3DRTYPE_VERTEXBUFFER);
    MG_CHECK(vertex->SetPriority(7) == 0 && vertex->GetPriority() == 7);
    MG_CHECK(vertex->SetPriority(3) == 7);

    void *bytes = nullptr;
    MG_CHECK(vertex->Lock(0, 0, &bytes, D3DLOCK_DISCARD) == D3D_OK);
    std::memset(bytes, 42, 64);
    MG_CHECK(vertex->Unlock() == D3D_OK);
    MG_CHECK(vertex->Lock(1, 0, &bytes, D3DLOCK_NOOVERWRITE) == D3D_OK);
    const bool retained = static_cast<unsigned char *>(bytes)[0] == 42 &&
        static_cast<unsigned char *>(bytes)[62] == 42;
    MG_CHECK(vertex->Unlock() == D3D_OK);
    MG_CHECK(retained);

    IDirect3DIndexBuffer9 *raw = nullptr;
    const auto result = device.get()->CreateIndexBuffer(8, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
        D3DPOOL_MANAGED, &raw, nullptr);
    ComPtr<IDirect3DIndexBuffer9> index(raw);
    MG_CHECK(result == D3D_OK && index);
    D3DINDEXBUFFER_DESC index_description{};
    MG_CHECK(index->GetDesc(&index_description) == D3D_OK);
    MG_CHECK(index_description.Size == 8 && index_description.Format == D3DFMT_INDEX16);
    MG_CHECK(index_description.Type == D3DRTYPE_INDEXBUFFER && index_description.Pool == D3DPOOL_MANAGED);
    MG_CHECK(index->Lock(2, 4, &bytes, 0) == D3D_OK);
    std::memset(bytes, 11, 4);
    MG_CHECK(index->Unlock() == D3D_OK);
    MG_CHECK(index->Lock(0, 0, &bytes, 0) == D3D_OK);
    const auto *values = static_cast<const unsigned char *>(bytes);
    const bool bounds_preserved = values[0] == 0 && values[1] == 0 && values[2] == 11 &&
        values[5] == 11 && values[6] == 0 && values[7] == 0;
    MG_CHECK(index->Unlock() == D3D_OK);
    MG_CHECK(bounds_preserved);
    MG_CHECK(device.stats().live_buffers == 2 && device.stats().buffer_bytes == 72);
    MG_CHECK(device.stats().buffers_created == 2 && device.stats().locks == 4);
    vertex.reset();
    MG_CHECK(device.stats().live_buffers == 1 && device.stats().buffer_bytes == 8);
    index.reset();
    MG_CHECK(device.stats().live_buffers == 0 && device.stats().buffer_bytes == 0);
}

void invalid_buffers() {
    tests::BattleDevice device;
    auto vertex = vertex_buffer(device);
    void *bytes = nullptr;
    MG_CHECK(vertex->Lock(0, 0, &bytes, 0) == D3D_OK);
    void *nested = bytes;
    const auto nested_result = vertex->Lock(0, 1, &nested, 0);
    MG_CHECK(vertex->Unlock() == D3D_OK);
    MG_CHECK(nested_result == D3DERR_INVALIDCALL && nested == nullptr);
    MG_CHECK(vertex->Unlock() == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->Lock(63, 2, &bytes, 0) == D3DERR_INVALIDCALL && bytes == nullptr);
    MG_CHECK(vertex->Lock(65, 0, &bytes, 0) == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->Lock(1, std::numeric_limits<UINT>::max(), &bytes, 0) == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->Lock(0, 1, nullptr, 0) == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->Lock(0, 1, &bytes, D3DLOCK_READONLY) == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->Lock(0, 1, &bytes, 0x80000000u) == D3DERR_INVALIDCALL);
    MG_CHECK(vertex->GetDesc(nullptr) == D3DERR_INVALIDCALL);
    IDirect3DVertexBuffer9 *bad_vertex = vertex.get();
    MG_CHECK(device.get()->CreateVertexBuffer(0, 0, 0, D3DPOOL_DEFAULT, &bad_vertex, nullptr) == D3DERR_INVALIDCALL);
    MG_CHECK(bad_vertex == nullptr);
    MG_CHECK(device.get()->CreateVertexBuffer(std::numeric_limits<UINT>::max(), 0, 0,
        D3DPOOL_DEFAULT, &bad_vertex, nullptr) == D3DERR_INVALIDCALL);
    IDirect3DIndexBuffer9 *bad_index = nullptr;
    MG_CHECK(device.get()->CreateIndexBuffer(8, 0, D3DFMT_VERTEXDATA,
        D3DPOOL_DEFAULT, &bad_index, nullptr) == D3DERR_INVALIDCALL && bad_index == nullptr);
    MG_CHECK(device.stats().live_buffers == 1 && device.stats().buffers_created == 1);
}

template<class T> void identity(T *object, REFIID supported, REFIID unrelated) {
    void *queried = nullptr;
    MG_CHECK(object->QueryInterface(supported, &queried) == S_OK);
    ComPtr<T> owned(static_cast<T *>(queried));
    MG_CHECK(owned.get() == object);
    queried = nullptr;
    MG_CHECK(object->QueryInterface(__uuidof(IUnknown), &queried) == S_OK);
    ComPtr<IUnknown> unknown(static_cast<IUnknown *>(queried));
    MG_CHECK(unknown.get() == static_cast<IUnknown *>(object));
    queried = object;
    MG_CHECK(object->QueryInterface(unrelated, &queried) == E_NOINTERFACE);
    MG_CHECK(queried == nullptr);
    MG_CHECK(object->QueryInterface(supported, nullptr) == E_POINTER);
}

void com_ownership() {
    tests::BattleDevice device;
    identity(device.get(), __uuidof(IDirect3DDevice9), __uuidof(IDirect3DTexture9));
    auto vertex = vertex_buffer(device);
    identity(vertex.get(), __uuidof(IDirect3DVertexBuffer9), __uuidof(IDirect3DIndexBuffer9));
    void *queried = nullptr;
    MG_CHECK(vertex->QueryInterface(__uuidof(IDirect3DResource9), &queried) == S_OK);
    ComPtr<IDirect3DResource9> resource(static_cast<IDirect3DResource9 *>(queried));
    MG_CHECK(resource->GetType() == D3DRTYPE_VERTEXBUFFER);
    IDirect3DDevice9 *raw_device = nullptr;
    MG_CHECK(resource->GetDevice(&raw_device) == D3D_OK);
    ComPtr<IDirect3DDevice9> retained_device(raw_device);
    MG_CHECK(retained_device.get() == device.get());
    MG_CHECK(resource->GetDevice(nullptr) == D3DERR_INVALIDCALL);
    vertex.reset();
    MG_CHECK(device.stats().live_buffers == 1); // The queried resource still owns the buffer.
    resource.reset();
    MG_CHECK(device.stats().live_buffers == 0);

    IDirect3DIndexBuffer9 *raw_index = nullptr;
    MG_CHECK(device.get()->CreateIndexBuffer(8, 0, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &raw_index, nullptr) == D3D_OK);
    ComPtr<IDirect3DIndexBuffer9> index(raw_index);
    identity(index.get(), __uuidof(IDirect3DIndexBuffer9), __uuidof(IDirect3DVertexBuffer9));
    MG_CHECK(index->GetType() == D3DRTYPE_INDEXBUFFER);
}

void state_and_sessions() {
    MG_CHECK(g_D3DD == nullptr);
    const auto precision = _control87(0, 0) & _MCW_PC;
    for (int trial = 0; trial < 3; ++trial) {
        {
            tests::BattleDevice fixture;
            auto *device = fixture.get();
            MG_CHECK(g_D3DD == device && (_control87(0, 0) & _MCW_PC) == precision);
            MG_CHECK(fixture.stats().live_buffers == 0 && fixture.stats().material_updates == 0);
            D3DMATERIAL9 material{}, actual{};
            material.Diffuse.r = 0.375f;
            MG_CHECK(device->SetMaterial(&material) == D3D_OK);
            MG_CHECK(device->GetMaterial(&actual) == D3D_OK && actual.Diffuse.r == material.Diffuse.r);
            MG_CHECK(device->SetMaterial(nullptr) == D3DERR_INVALIDCALL);
            MG_CHECK(fixture.stats().material_updates == 1);
            D3DVIEWPORT9 viewport{5, 7, 640, 480, 0.0f, 1.0f}, read_viewport{};
            MG_CHECK(device->SetViewport(&viewport) == D3D_OK);
            MG_CHECK(device->GetViewport(&read_viewport) == D3D_OK);
            MG_CHECK(read_viewport.X == 5 && read_viewport.Y == 7 && read_viewport.Width == 640 && read_viewport.Height == 480);
            D3DMATRIX matrix{}, read_matrix{};
            matrix._11 = matrix._22 = matrix._33 = matrix._44 = 1.0f;
            matrix._41 = 32.0f;
            MG_CHECK(device->SetTransform(D3DTS_VIEW, &matrix) == D3D_OK);
            MG_CHECK(device->GetTransform(D3DTS_VIEW, &read_matrix) == D3D_OK && read_matrix._41 == 32.0f);
            DWORD state{};
            MG_CHECK(device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE) == D3D_OK);
            MG_CHECK(device->GetRenderState(D3DRS_ZENABLE, &state) == D3D_OK && state == D3DZB_FALSE);
            MG_CHECK(device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP) == D3D_OK);
            MG_CHECK(device->GetSamplerState(0, D3DSAMP_ADDRESSU, &state) == D3D_OK && state == D3DTADDRESS_CLAMP);
            MG_CHECK(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE) == D3D_OK);
            MG_CHECK(device->GetTextureStageState(0, D3DTSS_COLOROP, &state) == D3D_OK && state == D3DTOP_MODULATE);
            MG_CHECK(device->SetFVF(D3DFVF_XYZ) == D3D_OK);
            MG_CHECK(device->GetFVF(&state) == D3D_OK && state == D3DFVF_XYZ);
            MG_CHECK(device->ShowCursor(TRUE) == FALSE && device->ShowCursor(FALSE) == TRUE);
        }
        MG_CHECK(g_D3DD == nullptr && (_control87(0, 0) & _MCW_PC) == precision);
    }
}

constexpr tests::Case cases[] = {
    {"game.harness.device.buffers", buffers},
    {"game.harness.device.invalid_buffers", invalid_buffers},
    {"game.harness.device.com_ownership", com_ownership},
    {"game.harness.device.state_sessions", state_and_sessions},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::set_terminate([] { std::abort(); });
    return tests::run(argc, argv, cases);
}
