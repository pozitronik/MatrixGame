// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_device.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <vector>
#include <utility>

extern IDirect3DDevice9 *g_D3DD;

namespace tests {
namespace {
[[noreturn]] void unsupported(const char *method) {
    std::fprintf(stderr, "No-render battle device: unsupported call %s\n", method);
    std::fflush(stderr);
    std::abort();
}

// Bounds also keep a mistaken production allocation from exhausting the runner.
constexpr std::size_t maximum_buffer_bytes = 64u * 1024u * 1024u;
constexpr std::size_t maximum_total_bytes = 128u * 1024u * 1024u;
constexpr UINT maximum_texture_dimension = 4096;

class Texture final : public IDirect3DTexture9 {
    ULONG references_ = 1;
    IDirect3DDevice9 *device_;
    BattleDeviceStats &stats_;
    D3DSURFACE_DESC description_;
    std::vector<unsigned char> bytes_;
    DWORD priority_{};
    bool locked_{};
public:
    Texture(IDirect3DDevice9 *device, BattleDeviceStats &stats, UINT width, UINT height)
      : device_(device), stats_(stats),
        description_{D3DFMT_A8R8G8B8, D3DRTYPE_SURFACE, 0, D3DPOOL_MANAGED,
            D3DMULTISAMPLE_NONE, 0, width, height}, bytes_(std::size_t(width) * height * 4) {
        device_->AddRef();
        ++stats_.live_textures;
        ++stats_.textures_created;
        stats_.texture_bytes += bytes_.size();
    }
    ~Texture() {
        if (locked_) unsupported("destroy locked texture");
        --stats_.live_textures;
        stats_.texture_bytes -= bytes_.size();
        device_->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IDirect3DResource9)) ||
            IsEqualIID(iid, __uuidof(IDirect3DBaseTexture9)) || IsEqualIID(iid, __uuidof(IDirect3DTexture9))) {
            *out = static_cast<IDirect3DTexture9 *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = device_;
        device_->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override {
        unsupported("texture SetPrivateData");
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override {
        unsupported("texture GetPrivateData");
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { unsupported("texture FreePrivateData"); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD value) override {
        const DWORD previous = priority_;
        priority_ = value;
        return previous;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority_; }
    void STDMETHODCALLTYPE PreLoad() override {} // CPU storage is already resident.
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { unsupported("texture SetLOD"); }
    DWORD STDMETHODCALLTYPE GetLOD() override { unsupported("texture GetLOD"); }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override {
        unsupported("texture SetAutoGenFilterType");
    }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override {
        unsupported("texture GetAutoGenFilterType");
    }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override { unsupported("texture GenerateMipSubLevels"); }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *out) override {
        if (level != 0 || !out) return D3DERR_INVALIDCALL;
        *out = description_;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT, IDirect3DSurface9 **) override {
        unsupported("texture GetSurfaceLevel");
    }
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *out, const RECT *rect, DWORD flags) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = {};
        constexpr DWORD supported = D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_NO_DIRTY_UPDATE;
        if (level != 0 || locked_ || (flags & ~supported)) return D3DERR_INVALIDCALL;
        const RECT area = rect ? *rect : RECT{0, 0, LONG(description_.Width), LONG(description_.Height)};
        if (area.left < 0 || area.top < 0 || area.right <= area.left || area.bottom <= area.top ||
            area.right > LONG(description_.Width) || area.bottom > LONG(description_.Height))
            return D3DERR_INVALIDCALL;
        out->Pitch = INT(description_.Width * 4);
        out->pBits = bytes_.data() + std::size_t(area.top) * out->Pitch + std::size_t(area.left) * 4;
        locked_ = true;
        ++stats_.texture_locks;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override {
        if (level != 0 || !locked_) return D3DERR_INVALIDCALL;
        locked_ = false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *) override { unsupported("texture AddDirtyRect"); }
};

template<class Interface, class Desc>
class Buffer final : public Interface {
    ULONG references_ = 1;
    IDirect3DDevice9 *device_;
    BattleDeviceStats &stats_;
    Desc description_;
    std::vector<unsigned char> bytes_;
    DWORD priority_{};
    bool locked_{};
public:
    Buffer(IDirect3DDevice9 *device, BattleDeviceStats &stats, Desc description)
      : device_(device), stats_(stats), description_(description), bytes_(description.Size) {
        device_->AddRef();
        ++stats_.live_buffers;
        ++stats_.buffers_created;
        stats_.buffer_bytes += bytes_.size();
    }
    ~Buffer() {
        if (locked_) unsupported("destroy locked buffer");
        --stats_.live_buffers;
        stats_.buffer_bytes -= bytes_.size();
        device_->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IDirect3DResource9)) ||
            IsEqualIID(iid, __uuidof(Interface))) {
            *out = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = device_;
        device_->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override {
        unsupported("buffer SetPrivateData");
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override {
        unsupported("buffer GetPrivateData");
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { unsupported("buffer FreePrivateData"); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD value) override {
        const DWORD previous = priority_;
        priority_ = value;
        return previous;
    }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority_; }
    void STDMETHODCALLTYPE PreLoad() override {} // CPU storage is already resident.
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return description_.Type; }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void **out, DWORD flags) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        constexpr DWORD supported = D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE | D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK;
        if (locked_ || offset > bytes_.size() || (flags & ~supported)) return D3DERR_INVALIDCALL;
        if ((flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)) && !(description_.Usage & D3DUSAGE_DYNAMIC))
            return D3DERR_INVALIDCALL;
        if ((flags & D3DLOCK_READONLY) && (description_.Usage & D3DUSAGE_WRITEONLY)) return D3DERR_INVALIDCALL;
        if ((flags & D3DLOCK_READONLY) && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)))
            return D3DERR_INVALIDCALL;
        const std::size_t length = size ? size : bytes_.size() - offset;
        if (!length || length > bytes_.size() - offset) return D3DERR_INVALIDCALL;
        if (flags & D3DLOCK_DISCARD) std::fill(bytes_.begin(), bytes_.end(), 0);
        locked_ = true;
        ++stats_.locks;
        *out = bytes_.data() + offset;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override {
        if (!locked_) return D3DERR_INVALIDCALL;
        locked_ = false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(Desc *out) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = description_;
        return D3D_OK;
    }
};
} // namespace

class BattleDevice::Device final : public IDirect3DDevice9 {
    ULONG references_ = 1;
    D3DMATERIAL9 material_{};
    D3DVIEWPORT9 viewport_{0, 0, 1280, 720, 0.0f, 1.0f};
    std::map<D3DTRANSFORMSTATETYPE, D3DMATRIX> transforms_;
    std::map<D3DRENDERSTATETYPE, DWORD> render_states_;
    std::map<std::pair<DWORD, D3DTEXTURESTAGESTATETYPE>, DWORD> texture_states_;
    std::map<std::pair<DWORD, D3DSAMPLERSTATETYPE>, DWORD> sampler_states_;
    DWORD fvf_{};
    BOOL cursor_visible_{};
public:
    std::function<HRESULT(D3DPRESENT_PARAMETERS *)> reset_handler;
    BattleDeviceStats statistics;
    IconTargets icon_targets_;
    explicit Device(IconTargets icon_targets) : icon_targets_(icon_targets) {
        D3DMATRIX identity{};
        identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
        transforms_.emplace(D3DTS_WORLD, identity);
        transforms_.emplace(D3DTS_VIEW, identity);
        transforms_.emplace(D3DTS_PROJECTION, identity);
    }
    ~Device() {
        if (statistics.live_buffers || statistics.live_textures) unsupported("device destroyed with live resources");
    }
    ULONG reference_count() const { return references_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDirect3DDevice9))) {
            *ppvObject = static_cast<IDirect3DDevice9 *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT cooperative_level = D3D_OK;
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {
        return cooperative_level;
    }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override {
        unsupported("GetAvailableTextureMem");
    }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override {
        unsupported("EvictManagedResources");
    }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9**) override {
        unsupported("GetDirect3D");
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*) override {
        unsupported("GetDeviceCaps");
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE*) override {
        unsupported("GetDisplayMode");
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *) override {
        unsupported("GetCreationParameters");
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override {
        unsupported("SetCursorProperties");
    }
    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override {
        (void)X;
        (void)Y;
        (void)Flags;
    }
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override {
        const BOOL previous = cursor_visible_;
        cursor_visible_ = bShow;
        return previous;
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) override {
        unsupported("CreateAdditionalSwapChain");
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9**) override {
        unsupported("GetSwapChain");
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override {
        unsupported("GetNumberOfSwapChains");
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *parameters) override {
        if (!reset_handler) unsupported("Reset");
        return reset_handler(parameters);
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT *, const RECT *, HWND, const RGNDATA *) override {
        unsupported("Present");
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**) override {
        unsupported("GetBackBuffer");
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS*) override {
        unsupported("GetRasterStatus");
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL) override {
        unsupported("SetDialogBoxMode");
    }
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *) override {
        unsupported("SetGammaRamp");
    }
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP*) override {
        unsupported("GetGammaRamp");
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *shared_handle) override {
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        if (!width || !height || width > maximum_texture_dimension || height > maximum_texture_dimension || shared_handle)
            return D3DERR_INVALIDCALL;
        if (levels != 1 || usage != 0 || format != D3DFMT_A8R8G8B8 || pool != D3DPOOL_MANAGED)
            unsupported("CreateTexture profile (requires one managed A8R8G8B8 level, usage 0)");
        const std::size_t bytes = std::size_t(width) * height * 4;
        if (statistics.buffer_bytes + statistics.texture_bytes > maximum_total_bytes - bytes)
            return D3DERR_INVALIDCALL;
        try {
            *out = new Texture(this, statistics, width, height);
        }
        catch (const std::bad_alloc &) {
            return E_OUTOFMEMORY;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*) override {
        unsupported("CreateVolumeTexture");
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*) override {
        unsupported("CreateCubeTexture");
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override {
        if (!ppVertexBuffer) return D3DERR_INVALIDCALL;
        *ppVertexBuffer = nullptr;
        if (!Length || Length > maximum_buffer_bytes || pSharedHandle ||
            statistics.buffer_bytes + statistics.texture_bytes > maximum_total_bytes - Length)
            return D3DERR_INVALIDCALL;
        D3DVERTEXBUFFER_DESC desc{D3DFMT_VERTEXDATA, D3DRTYPE_VERTEXBUFFER, Usage, Pool, Length, FVF};
        try {
            *ppVertexBuffer = new Buffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>(this, statistics, desc);
        }
        catch (const std::bad_alloc &) {
            return E_OUTOFMEMORY;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override {
        if (!ppIndexBuffer) return D3DERR_INVALIDCALL;
        *ppIndexBuffer = nullptr;
        if (!Length || Length > maximum_buffer_bytes || pSharedHandle ||
            statistics.buffer_bytes + statistics.texture_bytes > maximum_total_bytes - Length ||
            (Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32)) return D3DERR_INVALIDCALL;
        D3DINDEXBUFFER_DESC desc{Format, D3DRTYPE_INDEXBUFFER, Usage, Pool, Length};
        try {
            *ppIndexBuffer = new Buffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>(this, statistics, desc);
        }
        catch (const std::bad_alloc &) {
            return E_OUTOFMEMORY;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample, DWORD quality, BOOL lockable, IDirect3DSurface9 **out, HANDLE *shared) override {
        if (icon_targets_ != IconTargets::Unavailable) unsupported("CreateRenderTarget");
        if (out) *out = nullptr;
        if (!out || width != 256 || height != 256 || format != D3DFMT_A8R8G8B8 ||
            multisample != D3DMULTISAMPLE_NONE || quality != 0 || !lockable || shared)
            return D3DERR_INVALIDCALL;
        ++statistics.rejected_icon_targets;
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*) override {
        unsupported("CreateDepthStencilSurface");
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const POINT *) override {
        unsupported("UpdateSurface");
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*, IDirect3DBaseTexture9*) override {
        unsupported("UpdateTexture");
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*, IDirect3DSurface9*) override {
        unsupported("GetRenderTargetData");
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9*) override {
        unsupported("GetFrontBufferData");
    }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *, D3DTEXTUREFILTERTYPE) override {
        unsupported("StretchRect");
    }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *, const RECT *, D3DCOLOR) override {
        unsupported("ColorFill");
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*) override {
        unsupported("CreateOffscreenPlainSurface");
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD, IDirect3DSurface9*) override {
        unsupported("SetRenderTarget");
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD, IDirect3DSurface9**) override {
        unsupported("GetRenderTarget");
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9*) override {
        unsupported("SetDepthStencilSurface");
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9**) override {
        unsupported("GetDepthStencilSurface");
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override {
        unsupported("BeginScene");
    }
    HRESULT STDMETHODCALLTYPE EndScene() override {
        unsupported("EndScene");
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD) override {
        unsupported("Clear");
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override {
        if (!matrix) return D3DERR_INVALIDCALL;
        transforms_[state] = *matrix;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override {
        const auto entry = transforms_.find(State);
        if (!pMatrix || entry == transforms_.end()) return D3DERR_INVALIDCALL;
        *pMatrix = entry->second;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override {
        unsupported("MultiplyTransform");
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *viewport) override {
        if (!viewport || !viewport->Width || !viewport->Height || viewport->MinZ < 0 || viewport->MaxZ > 1 || viewport->MinZ > viewport->MaxZ) return D3DERR_INVALIDCALL;
        viewport_ = *viewport;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override {
        if (!pViewport) return D3DERR_INVALIDCALL;
        *pViewport = viewport_;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *material) override {
        if (!material) return D3DERR_INVALIDCALL;
        material_ = *material;
        ++statistics.material_updates;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override {
        if (!pMaterial) return D3DERR_INVALIDCALL;
        *pMaterial = material_;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, const D3DLIGHT9 *) override {
        unsupported("SetLight");
    }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD, D3DLIGHT9*) override {
        unsupported("GetLight");
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override {
        unsupported("LightEnable");
    }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL*) override {
        unsupported("GetLightEnable");
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, const float *) override {
        unsupported("SetClipPlane");
    }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float*) override {
        unsupported("GetClipPlane");
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
        render_states_[State] = Value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override {
        const auto entry = render_states_.find(State);
        if (!pValue || entry == render_states_.end()) return D3DERR_INVALIDCALL;
        *pValue = entry->second;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**) override {
        unsupported("CreateStateBlock");
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override {
        unsupported("BeginStateBlock");
    }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9**) override {
        unsupported("EndStateBlock");
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *) override {
        unsupported("SetClipStatus");
    }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9*) override {
        unsupported("GetClipStatus");
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, IDirect3DBaseTexture9**) override {
        unsupported("GetTexture");
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD, IDirect3DBaseTexture9*) override {
        unsupported("SetTexture");
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override {
        const auto entry = texture_states_.find({Stage, Type});
        if (!pValue || entry == texture_states_.end()) return D3DERR_INVALIDCALL;
        *pValue = entry->second;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
        texture_states_[{Stage, Type}] = Value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override {
        const auto entry = sampler_states_.find({Sampler, Type});
        if (!pValue || entry == sampler_states_.end()) return D3DERR_INVALIDCALL;
        *pValue = entry->second;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override {
        sampler_states_[{Sampler, Type}] = Value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD*) override {
        unsupported("ValidateDevice");
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY *) override {
        unsupported("SetPaletteEntries");
    }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT,PALETTEENTRY*) override {
        unsupported("GetPaletteEntries");
    }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override {
        unsupported("SetCurrentTexturePalette");
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *) override {
        unsupported("GetCurrentTexturePalette");
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *) override {
        unsupported("SetScissorRect");
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT*) override {
        unsupported("GetScissorRect");
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL) override {
        unsupported("SetSoftwareVertexProcessing");
    }
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override {
        unsupported("GetSoftwareVertexProcessing");
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override {
        unsupported("SetNPatchMode");
    }
    float STDMETHODCALLTYPE GetNPatchMode() override {
        unsupported("GetNPatchMode");
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) override {
        unsupported("DrawPrimitive");
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT) override {
        unsupported("DrawIndexedPrimitive");
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *, UINT) override {
        unsupported("DrawPrimitiveUP");
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT) override {
        unsupported("DrawIndexedPrimitiveUP");
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) override {
        unsupported("ProcessVertices");
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, IDirect3DVertexDeclaration9 **) override {
        unsupported("CreateVertexDeclaration");
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9*) override {
        unsupported("SetVertexDeclaration");
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9**) override {
        unsupported("GetVertexDeclaration");
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override {
        fvf_ = FVF;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override {
        if (!pFVF) return D3DERR_INVALIDCALL;
        *pFVF = fvf_;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *, IDirect3DVertexShader9 **) override {
        unsupported("CreateVertexShader");
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9*) override {
        unsupported("SetVertexShader");
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9**) override {
        unsupported("GetVertexShader");
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, const float *, UINT) override {
        unsupported("SetVertexShaderConstantF");
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT, float*, UINT) override {
        unsupported("GetVertexShaderConstantF");
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int *, UINT) override {
        unsupported("SetVertexShaderConstantI");
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int*, UINT) override {
        unsupported("GetVertexShaderConstantI");
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const BOOL *, UINT) override {
        unsupported("SetVertexShaderConstantB");
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, BOOL*, UINT) override {
        unsupported("GetVertexShaderConstantB");
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT, IDirect3DVertexBuffer9*, UINT, UINT) override {
        unsupported("SetStreamSource");
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT, IDirect3DVertexBuffer9**, UINT*, UINT*) override {
        unsupported("GetStreamSource");
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override {
        unsupported("SetStreamSourceFreq");
    }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT*) override {
        unsupported("GetStreamSourceFreq");
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9*) override {
        unsupported("SetIndices");
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9**) override {
        unsupported("GetIndices");
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *, IDirect3DPixelShader9 **) override {
        unsupported("CreatePixelShader");
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9*) override {
        unsupported("SetPixelShader");
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9**) override {
        unsupported("GetPixelShader");
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, const float *, UINT) override {
        unsupported("SetPixelShaderConstantF");
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT, float*, UINT) override {
        unsupported("GetPixelShaderConstantF");
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int *, UINT) override {
        unsupported("SetPixelShaderConstantI");
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int*, UINT) override {
        unsupported("GetPixelShaderConstantI");
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const BOOL *, UINT) override {
        unsupported("SetPixelShaderConstantB");
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, BOOL*, UINT) override {
        unsupported("GetPixelShaderConstantB");
    }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) override {
        unsupported("DrawRectPatch");
    }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) override {
        unsupported("DrawTriPatch");
    }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override {
        unsupported("DeletePatch");
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE, IDirect3DQuery9**) override {
        unsupported("CreateQuery");
    }
};

BattleDevice::BattleDevice(IconTargets icon_targets) : device_(nullptr), previous_(g_D3DD) {
    if (previous_) unsupported("install over an existing device");
    device_ = new Device(icon_targets);
    g_D3DD = device_;
}

BattleDevice::~BattleDevice() {
    if (g_D3DD != device_) unsupported("device alias changed during scenario");
    if (device_->statistics.live_buffers || device_->statistics.live_textures || device_->reference_count() != 1)
        unsupported("scenario leaked resources or device references");
    g_D3DD = previous_;
    device_->Release();
}

IDirect3DDevice9 *BattleDevice::get() const { return device_; }
void BattleDevice::cooperative_level(HRESULT status) {
    if (status != D3D_OK && status != D3DERR_DEVICELOST) unsupported("cooperative level other than OK or lost");
    device_->cooperative_level = status;
}
const BattleDeviceStats &BattleDevice::stats() const { return device_->statistics; }
void BattleDevice::reset_handler(std::function<HRESULT(D3DPRESENT_PARAMETERS *)> handler) {
    device_->reset_handler = std::move(handler);
}

} // namespace tests
