// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "Audio.hpp"
#include <stdexcept>
namespace audio_fixture {
inline void word(std::vector<uint8_t> &out, uint16_t n) { out.push_back(uint8_t(n)); out.push_back(uint8_t(n >> 8)); }
inline void dword(std::vector<uint8_t> &out, uint32_t n) { word(out, uint16_t(n)); word(out, uint16_t(n >> 16)); }
inline void tag(std::vector<uint8_t> &out, const char *name) { out.insert(out.end(), name, name + 4); }

inline std::vector<uint8_t> wave(uint16_t channels = 1, uint16_t bits = 16, uint32_t rate = 22255, bool odd_chunk = false) {
    std::vector<uint8_t> chunks;
    if (odd_chunk) { tag(chunks, "JUNK"); dword(chunks, 1); chunks.push_back(7); chunks.push_back(0); }
    tag(chunks, "fmt "); dword(chunks, 16); word(chunks, 1); word(chunks, channels);
    dword(chunks, rate); const uint16_t frame = uint16_t(channels * bits / 8);
    dword(chunks, rate * frame); word(chunks, frame); word(chunks, bits);
    tag(chunks, "data"); dword(chunks, 8);
    for (uint8_t i = 0; i < 8; ++i) chunks.push_back(i);
    std::vector<uint8_t> bytes; tag(bytes, "RIFF"); dword(bytes, uint32_t(chunks.size() + 4)); tag(bytes, "WAVE");
    bytes.insert(bytes.end(), chunks.begin(), chunks.end());
    return bytes;
}

struct State {
    bool playing{};
    bool loop{};
    bool fail_play{};
    float volume{1};
    float pan{};
    int destroyed{};
    std::weak_ptr<const Audio::Clip> clip;
};

struct DeviceState {
    std::vector<std::shared_ptr<State>> voices;
    bool fail_create{};
    bool destroyed{};
    bool live_clip_at_shutdown{};
};

class FakeVoice final : public Audio::Voice {
    std::shared_ptr<State> state_;
    std::shared_ptr<const Audio::Clip> clip_;
public:
    FakeVoice(std::shared_ptr<State> state, std::shared_ptr<const Audio::Clip> clip) : state_(std::move(state)), clip_(std::move(clip)) {}
    ~FakeVoice() override { state_->playing = false; ++state_->destroyed; }
    void play() override { if (state_->fail_play) throw std::runtime_error("Synthetic device failure"); state_->playing = true; }
    bool playing() const override { return state_->playing; }
    void volume(float n) override { state_->volume = n; }
    void pan(float n) override { state_->pan = n; }
};

class FakeDevice final : public Audio::Device {
    std::shared_ptr<DeviceState> state_;
public:
    explicit FakeDevice(std::shared_ptr<DeviceState> state) : state_(std::move(state)) {}
    ~FakeDevice() override {
        state_->destroyed = true;
        for (const auto &voice : state_->voices) state_->live_clip_at_shutdown |= !voice->clip.expired();
    }
    std::unique_ptr<Audio::Voice> create(std::shared_ptr<const Audio::Clip> clip, bool loop) override {
        if (state_->fail_create) throw std::runtime_error("Synthetic create failure");
        auto voice = std::make_shared<State>(); voice->loop = loop; voice->clip = clip;
        state_->voices.push_back(voice);
        return std::make_unique<FakeVoice>(voice, std::move(clip));
    }
};

struct Fixture {
    std::shared_ptr<DeviceState> device = std::make_shared<DeviceState>();
    int loads{};
    int reports{};
    bool fail_load{};
    Audio::Service service{std::make_unique<FakeDevice>(device), {{L"Sound.A", L"synthetic.wav"}, {L"Sound.B", L"synthetic.wav"}},
        [this](const std::wstring &) { ++loads; if (fail_load) throw std::runtime_error("Synthetic missing file"); return wave(); },
        [this](const std::wstring &, const std::string &) { ++reports; }};
};

} // namespace audio_fixture
