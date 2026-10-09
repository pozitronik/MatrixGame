// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Audio {
constexpr uint32_t invalid_id = UINT32_MAX;

struct Clip {
    uint16_t channels{};
    uint16_t bits{};
    uint32_t sample_rate{};
    std::vector<uint8_t> samples;
};

Clip read_wave(std::span<const uint8_t> bytes);

class Voice {
public:
    virtual ~Voice() = default;
    virtual void play() = 0;
    virtual bool playing() const = 0;
    virtual void volume(float value) = 0;
    virtual void pan(float value) = 0;
};

class Device {
public:
    virtual ~Device() = default;
    virtual std::unique_ptr<Voice> create(std::shared_ptr<const Clip> clip, bool loop) = 0;
};

using Mapping = std::unordered_map<std::wstring, std::wstring>;
using Loader = std::function<std::vector<uint8_t>(const std::wstring &)>;
using Reporter = std::function<void(const std::wstring &, const std::string &)>;

class Service {
    struct Entry {
        std::unique_ptr<Voice> voice;
        std::wstring name;
        float volume{1.0f};
        float pan{};
    };
    // Reverse member destruction releases voices and clip data before the device.
    std::unique_ptr<Device> device_;
    Mapping mapping_;
    Loader loader_;
    Reporter report_;
    std::unordered_map<std::wstring, std::shared_ptr<const Clip>> clips_;
    std::unordered_set<std::wstring> failures_;
    std::unordered_map<uint32_t, Entry> voices_;
    uint64_t next_id_{1};

    void failure(const std::wstring &name, const std::string &message);
    void voice_failure(uint32_t id, const std::string &message);

public:
    Service(std::unique_ptr<Device> device, Mapping mapping, Loader loader, Reporter report);
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    ~Service() = default;

    uint32_t create(const std::wstring &name, bool loop);
    void destroy(uint32_t id);
    void play(uint32_t id);
    bool playing(uint32_t id);
    void volume(uint32_t id, float value);
    void pan(uint32_t id, float value);
    float volume(uint32_t id) const;
    float pan(uint32_t id) const;
    void clear();
};

std::unique_ptr<Device> create_xaudio_device();
} // namespace Audio
