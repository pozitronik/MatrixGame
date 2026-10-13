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
};

// A storage/state-only device for the explicitly no-render scenario profile.
// Creates no window or native device and never changes floating-point precision.
// Unknown calls (including rendering, textures and Reset) terminate the test
// with the method name, even if the production caller ignores its HRESULT.
class BattleDevice {
    class Device;
    Device *device_;
    IDirect3DDevice9 *previous_;
public:
    BattleDevice();
    ~BattleDevice();
    BattleDevice(const BattleDevice &) = delete;
    BattleDevice &operator=(const BattleDevice &) = delete;
    IDirect3DDevice9 *get() const;
    const BattleDeviceStats &stats() const;
};

} // namespace tests
