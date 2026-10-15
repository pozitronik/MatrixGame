// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "Audio.hpp"
#include "Music.hpp"
#include <algorithm>
#include <cstdint>
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
    bool fail_volume{};
    float volume{1};
    float pan{};
    int destroyed{};
    std::weak_ptr<const Audio::Clip> clip;
};

struct StreamState {
    uint16_t channels{};
    uint32_t sample_rate{};
    std::vector<std::vector<int16_t>> buffers;  // Every submitted buffer, in order.
    size_t queued{};                             // Decrement to simulate playback.
    bool drains{};                               // Plays each buffer as soon as it is submitted.
    int plays{};
    float volume{1};
    bool fail_submit{};
    bool fail_volume{};
    bool destroyed{};
};

struct DeviceState {
    std::vector<std::shared_ptr<State>> voices;
    std::vector<std::shared_ptr<StreamState>> streams;
    bool fail_stream{};
    bool drain_streams{};
    bool fail_create{};
    std::string critical_error;  // Reported by the device once set.
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
    void volume(float n) override {
        if (state_->fail_volume) throw std::runtime_error("Synthetic volume failure");
        state_->volume = n;
    }
    void pan(float n) override { state_->pan = n; }
};

class FakeStream final : public Audio::Stream {
    std::shared_ptr<StreamState> state_;
public:
    explicit FakeStream(std::shared_ptr<StreamState> state) : state_(std::move(state)) {}
    ~FakeStream() override { state_->destroyed = true; }
    void submit(std::vector<int16_t> samples) override {
        if (state_->fail_submit) throw std::runtime_error("Synthetic submit failure");
        state_->buffers.push_back(std::move(samples));
        if (!state_->drains) ++state_->queued;
    }
    size_t queued() const override { return state_->queued; }
    void play() override { ++state_->plays; }
    void volume(float n) override {
        if (state_->fail_volume) throw std::runtime_error("Synthetic music volume failure");
        state_->volume = n;
    }
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
    std::unique_ptr<Audio::Stream> stream(uint16_t channels, uint32_t sample_rate) override {
        if (state_->fail_stream) throw std::runtime_error("Synthetic stream failure");
        auto stream = std::make_shared<StreamState>();
        stream->channels = channels;
        stream->sample_rate = sample_rate;
        stream->drains = state_->drain_streams;
        state_->streams.push_back(stream);
        return std::make_unique<FakeStream>(stream);
    }
    std::string critical_error() const override { return state_->critical_error; }
};

// Synthetic PCM track. Sample i of the track holds base + i, so its origin and order are visible.
struct TrackSpec {
    uint16_t channels{1};
    uint32_t sample_rate{8000};
    size_t frames{10000};
    int16_t base{};
    size_t fail_after{SIZE_MAX};  // Samples decoded before a synthetic decode error.
};

class FakeDecoder final : public Audio::Decoder {
    TrackSpec spec_;
    size_t position_{};
public:
    explicit FakeDecoder(TrackSpec spec) : spec_(spec) {}
    uint16_t channels() const override { return spec_.channels; }
    uint32_t sample_rate() const override { return spec_.sample_rate; }
    size_t read(std::span<int16_t> out) override {
        if (position_ >= spec_.fail_after) throw std::runtime_error("Synthetic decode failure");
        const size_t total = spec_.frames * spec_.channels;
        size_t count = std::min(out.size() / spec_.channels * spec_.channels, total - position_);
        count = std::min(count, spec_.fail_after - position_);
        for (size_t i = 0; i < count; ++i) out[i] = int16_t(spec_.base + int16_t(position_ + i));
        position_ += count;
        return count;
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
