// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_device.hpp"
#include "test_support.hpp"
#include "Texture.hpp"

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

ComPtr<IDirect3DTexture9> texture(tests::BattleDevice &device) {
    IDirect3DTexture9 *raw = nullptr;
    const auto result = device.get()->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &raw, nullptr);
    ComPtr<IDirect3DTexture9> owned(raw);
    MG_CHECK(result == D3D_OK && owned);
    return owned;
}

void textures() {
    tests::BattleDevice device;
    auto image = texture(device);
    D3DSURFACE_DESC desc{};
    MG_CHECK(image->GetLevelCount() == 1 && image->GetType() == D3DRTYPE_TEXTURE);
    MG_CHECK(image->GetLevelDesc(0, &desc) == D3D_OK);
    MG_CHECK(desc.Width == 2 && desc.Height == 2 && desc.Format == D3DFMT_A8R8G8B8);
    MG_CHECK(desc.Type == D3DRTYPE_SURFACE && desc.Pool == D3DPOOL_MANAGED && desc.Usage == 0);
    MG_CHECK(desc.MultiSampleType == D3DMULTISAMPLE_NONE && desc.MultiSampleQuality == 0);
    MG_CHECK(image->SetPriority(9) == 0 && image->GetPriority() == 9);
    MG_CHECK(image->SetPriority(2) == 9);
    image->PreLoad();

    D3DLOCKED_RECT lock{};
    MG_CHECK(image->LockRect(0, &lock, nullptr, 0) == D3D_OK);
    MG_CHECK(lock.Pitch == 8 && lock.pBits);
    std::memset(lock.pBits, 17, 16);
    MG_CHECK(image->UnlockRect(0) == D3D_OK);
    const RECT right_column{1, 0, 2, 2};
    MG_CHECK(image->LockRect(0, &lock, &right_column, D3DLOCK_NOSYSLOCK) == D3D_OK);
    auto *bytes = static_cast<unsigned char *>(lock.pBits);
    std::memset(bytes, 23, 4);
    std::memset(bytes + lock.Pitch, 42, 4);
    MG_CHECK(image->UnlockRect(0) == D3D_OK);
    MG_CHECK(image->LockRect(0, &lock, nullptr, D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE) == D3D_OK);
    const auto *read = static_cast<const unsigned char *>(lock.pBits);
    bool retained = true;
    for (int i = 0; i < 16; ++i)
        retained &= read[i] == (i < 4 || (i >= 8 && i < 12) ? 17 : i < 8 ? 23 : 42);
    MG_CHECK(image->UnlockRect(0) == D3D_OK);
    MG_CHECK(retained);
    MG_CHECK(device.stats().live_textures == 1 && device.stats().texture_bytes == 16);
    MG_CHECK(device.stats().textures_created == 1 && device.stats().texture_locks == 3);
    image.reset();
    MG_CHECK(device.stats().live_textures == 0 && device.stats().texture_bytes == 0);
}

void invalid_textures() {
    tests::BattleDevice device;
    auto image = texture(device);
    D3DSURFACE_DESC desc{};
    MG_CHECK(image->GetLevelDesc(1, &desc) == D3DERR_INVALIDCALL);
    MG_CHECK(image->GetLevelDesc(0, nullptr) == D3DERR_INVALIDCALL);
    D3DLOCKED_RECT lock{};
    MG_CHECK(image->LockRect(0, &lock, nullptr, 0) == D3D_OK);
    D3DLOCKED_RECT nested = lock;
    const auto result = image->LockRect(0, &nested, nullptr, 0);
    MG_CHECK(image->UnlockRect(1) == D3DERR_INVALIDCALL);
    MG_CHECK(image->UnlockRect(0) == D3D_OK);
    MG_CHECK(result == D3DERR_INVALIDCALL && nested.pBits == nullptr && nested.Pitch == 0);
    MG_CHECK(image->UnlockRect(0) == D3DERR_INVALIDCALL);
    MG_CHECK(image->LockRect(1, &lock, nullptr, 0) == D3DERR_INVALIDCALL && !lock.pBits);
    MG_CHECK(image->LockRect(0, nullptr, nullptr, 0) == D3DERR_INVALIDCALL);
    MG_CHECK(image->LockRect(0, &lock, nullptr, D3DLOCK_DISCARD) == D3DERR_INVALIDCALL);
    MG_CHECK(image->LockRect(0, &lock, nullptr, D3DLOCK_NOOVERWRITE) == D3DERR_INVALIDCALL);
    MG_CHECK(image->LockRect(0, &lock, nullptr, 0x80000000u) == D3DERR_INVALIDCALL);
    const RECT invalid[]{{-1, 0, 1, 1}, {0, -1, 1, 1}, {0, 0, 3, 1}, {0, 0, 1, 3},
        {0, 0, 0, 1}, {0, 0, 1, 0}, {1, 0, 0, 1}, {0, 1, 1, 0}};
    for (const auto &rect : invalid)
        MG_CHECK(image->LockRect(0, &lock, &rect, 0) == D3DERR_INVALIDCALL && !lock.pBits && !lock.Pitch);
    for (const UINT dimension : {0u, 4097u, std::numeric_limits<UINT>::max()}) {
        IDirect3DTexture9 *bad = image.get();
        MG_CHECK(device.get()->CreateTexture(dimension, 2, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &bad, nullptr) == D3DERR_INVALIDCALL && !bad);
        MG_CHECK(device.get()->CreateTexture(2, dimension, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &bad, nullptr) == D3DERR_INVALIDCALL && !bad);
    }
    MG_CHECK(device.get()->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, nullptr, nullptr) == D3DERR_INVALIDCALL);
    IDirect3DTexture9 *bad = image.get();
    HANDLE shared = nullptr;
    MG_CHECK(device.get()->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &bad, &shared) == D3DERR_INVALIDCALL && !bad);
    MG_CHECK(device.stats().textures_created == 1 && device.stats().texture_locks == 1);
}

void texture_ownership() {
    tests::BattleDevice device;
    auto image = texture(device);
    identity(image.get(), __uuidof(IDirect3DTexture9), __uuidof(IDirect3DSurface9));
    void *queried = nullptr;
    MG_CHECK(image->QueryInterface(__uuidof(IDirect3DBaseTexture9), &queried) == S_OK);
    ComPtr<IDirect3DBaseTexture9> base(static_cast<IDirect3DBaseTexture9 *>(queried));
    MG_CHECK(base.get() == static_cast<IDirect3DBaseTexture9 *>(image.get()));
    MG_CHECK(base->QueryInterface(__uuidof(IDirect3DResource9), &queried) == S_OK);
    ComPtr<IDirect3DResource9> resource(static_cast<IDirect3DResource9 *>(queried));
    MG_CHECK(resource->GetType() == D3DRTYPE_TEXTURE);
    IDirect3DDevice9 *raw = nullptr;
    MG_CHECK(resource->GetDevice(&raw) == D3D_OK);
    ComPtr<IDirect3DDevice9> retained(raw);
    MG_CHECK(retained.get() == device.get());
    MG_CHECK(resource->GetDevice(nullptr) == D3DERR_INVALIDCALL);
    image.reset();
    base.reset();
    MG_CHECK(device.stats().live_textures == 1);
    resource.reset();
    MG_CHECK(device.stats().live_textures == 0);
}

void managed_texture_upload() {
    tests::BattleDevice device;
    {
        CBitmap bitmap;
        bitmap.CreateRGBA(2, 2);
        const unsigned char rgba[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        for (int row = 0; row < 2; ++row)
            std::memcpy(bitmap.Data() + row * bitmap.Pitch(), rgba + row * 8, 8);
        CTextureManaged managed;
        managed.LoadFromBitmap(bitmap, false, 1);
        MG_CHECK(managed.DX() && managed.GetSizeX() == 2 && managed.GetSizeY() == 2);
        managed.Preload();
        D3DLOCKED_RECT lock{};
        managed.LockRect(lock, D3DLOCK_READONLY);
        bool converted = true;
        for (int row = 0; row < 2; ++row) {
            const auto *bytes = static_cast<const unsigned char *>(lock.pBits) + row * lock.Pitch;
            for (int col = 0; col < 2; ++col) {
                const auto *source = rgba + row * 8 + col * 4;
                converted &= bytes[col * 4] == source[2] && bytes[col * 4 + 1] == source[1] &&
                    bytes[col * 4 + 2] == source[0] && bytes[col * 4 + 3] == source[3];
            }
        }
        managed.UnlockRect();
        MG_CHECK(converted);
        CTextureManaged locked;
        MG_CHECK(locked.CreateLock(D3DFMT_A8R8G8B8, 2, 2, 1, lock) == D3D_OK);
        std::memset(lock.pBits, 53, 16);
        locked.UnlockRect();
        MG_CHECK(locked.GetPixelColor(1, 1) == 0x35353535u);
        MG_CHECK(device.stats().live_textures == 2);
    }
    MG_CHECK(device.stats().live_textures == 0 && device.stats().texture_bytes == 0);
}

void resource_budget() {
    tests::BattleDevice device;
    constexpr UINT block = 64u * 1024u * 1024u;
    IDirect3DTexture9 *raw_texture = nullptr;
    MG_CHECK(device.get()->CreateTexture(4096, 4096, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &raw_texture, nullptr) == D3D_OK);
    ComPtr<IDirect3DTexture9> image(raw_texture);
    IDirect3DVertexBuffer9 *raw_vertex = nullptr;
    MG_CHECK(device.get()->CreateVertexBuffer(block, 0, 0, D3DPOOL_MANAGED, &raw_vertex, nullptr) == D3D_OK);
    ComPtr<IDirect3DVertexBuffer9> vertex(raw_vertex);
    MG_CHECK(device.stats().texture_bytes == block && device.stats().buffer_bytes == block);
    IDirect3DTexture9 *bad_texture = nullptr;
    MG_CHECK(device.get()->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &bad_texture, nullptr) == D3DERR_INVALIDCALL && !bad_texture);
    IDirect3DVertexBuffer9 *bad_vertex = nullptr;
    MG_CHECK(device.get()->CreateVertexBuffer(4, 0, 0, D3DPOOL_MANAGED,
        &bad_vertex, nullptr) == D3DERR_INVALIDCALL && !bad_vertex);
    IDirect3DIndexBuffer9 *bad_index = nullptr;
    MG_CHECK(device.get()->CreateIndexBuffer(4, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
        &bad_index, nullptr) == D3DERR_INVALIDCALL && !bad_index);
    vertex.reset();
    auto replacement_texture = texture(device);
    MG_CHECK(device.stats().texture_bytes == block + 16 && device.stats().buffer_bytes == 0);
    image.reset();
    replacement_texture.reset();
    MG_CHECK(device.stats().texture_bytes == 0 && device.stats().live_textures == 0);
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

void unavailable_icon_targets() {
    for (int repeat = 0; repeat < 2; ++repeat) {
        tests::BattleDevice fixture(tests::BattleDevice::IconTargets::Unavailable);
        auto *device = fixture.get();
        IDirect3DSurface9 *surface = reinterpret_cast<IDirect3DSurface9 *>(1);
        MG_CHECK(device->CreateRenderTarget(256, 256, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
            0, TRUE, &surface, nullptr) == D3DERR_NOTAVAILABLE);
        MG_CHECK(!surface && fixture.stats().rejected_icon_targets == 1);
        MG_CHECK(device->CreateRenderTarget(128, 256, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
            0, TRUE, &surface, nullptr) == D3DERR_INVALIDCALL);
        MG_CHECK(device->CreateRenderTarget(256, 256, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
            0, TRUE, nullptr, nullptr) == D3DERR_INVALIDCALL);
        MG_CHECK(fixture.stats().rejected_icon_targets == 1);
        MG_CHECK(fixture.stats().live_buffers == 0 && fixture.stats().live_textures == 0);
    }
    tests::BattleDevice ordinary;
    MG_CHECK(ordinary.stats().rejected_icon_targets == 0);
}

constexpr tests::Case cases[] = {
    {"game.harness.device.unavailable_icon_targets", unavailable_icon_targets},
    {"game.harness.device.buffers", buffers},
    {"game.harness.device.invalid_buffers", invalid_buffers},
    {"game.harness.device.com_ownership", com_ownership},
    {"game.harness.device.state_sessions", state_and_sessions},
    {"game.harness.device.textures", textures},
    {"game.harness.device.invalid_textures", invalid_textures},
    {"game.harness.device.texture_ownership", texture_ownership},
    {"game.harness.device.managed_texture_upload", managed_texture_upload},
    {"game.harness.device.resource_budget", resource_budget},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::set_terminate([] { std::abort(); });
    return tests::run(argc, argv, cases);
}
