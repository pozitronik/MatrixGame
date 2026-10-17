// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <d3d9.h>
#include <cstddef>
#include <cstdint>

namespace tests {

struct BattleDeviceStats {
    std::size_t live_buffers{};
    std::size_t buffer_bytes{};
    std::uint64_t buffers_created{};
    std::uint64_t locks{};
    std::uint64_t material_updates{};
    std::size_t live_textures{};
    std::size_t texture_bytes{};
    std::uint64_t textures_created{};
    std::uint64_t texture_locks{};
    std::uint64_t rejected_icon_targets{};
    std::uint64_t texture_memory_queries{};
};

// A storage/state-only device for the explicitly no-render scenario profile.
// Creates no window or native device and never changes floating-point precision.
// Textures support one A8R8G8B8 mip level in managed CPU storage, without drawing.
// The loop's memory telemetry reports a fixed 64 MiB and counts queries; this
// is not a model of native texture-memory availability. This reply also permits
// production texture loaders to pass their available-memory gate; unsupported
// texture creation and rendering still fail at the corresponding device call.
// Opt-in IconTargets::Unavailable rejects only the 256x256 icon render target
// with D3DERR_NOTAVAILABLE; it never simulates successful drawing.
// Unknown calls (including rendering, other texture profiles and Reset) terminate the test
// with the method name, even if the production caller ignores its HRESULT.
// cooperative_level can report a lost device, which needs no Reset while it lasts.
class BattleDevice {
    class Device;
    Device *device_;
    IDirect3DDevice9 *previous_;
public:
    enum class IconTargets { Forbidden, Unavailable };
    explicit BattleDevice(IconTargets icon_targets = IconTargets::Forbidden);
    ~BattleDevice();
    BattleDevice(const BattleDevice &) = delete;
    BattleDevice &operator=(const BattleDevice &) = delete;
    IDirect3DDevice9 *get() const;
    void cooperative_level(HRESULT status);
    const BattleDeviceStats &stats() const;
};

} // namespace tests
