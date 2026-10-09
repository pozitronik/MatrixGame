// MatrixGame - licensed under GPLv2 or any later version.
#include "Audio.hpp"

#include <windows.h>
#include <xaudio2.h>

#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace Audio {
namespace {
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        char code[16]{};
        std::snprintf(code, sizeof(code), "0x%08lx", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(operation) + " failed: " + code);
    }
}

struct ComScope {
    bool owned{};
    ComScope() {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (result != RPC_E_CHANGED_MODE) check(result, "Audio COM initialization");
        owned = SUCCEEDED(result);
    }
    ~ComScope() { if (owned) CoUninitialize(); }
};

struct Library {
    HMODULE module{LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Library() {
        if (!module) throw std::runtime_error("XAudio2.9 is unavailable; standalone audio requires Windows 10 or later");
    }
    ~Library() { FreeLibrary(module); }
};

struct ReleaseEngine { void operator()(IXAudio2 *value) const { if (value) value->Release(); } };
struct DestroyVoice {
    template<class T> void operator()(T *value) const { if (value) value->DestroyVoice(); }
};

class XVoice final : public Voice {
    // DestroyVoice waits until processing no longer reads the clip.
    std::shared_ptr<const Clip> clip_;
    std::unique_ptr<IXAudio2SourceVoice, DestroyVoice> voice_;
    IXAudio2MasteringVoice *master_;
    bool started_{};

public:
    XVoice(IXAudio2 *engine, IXAudio2MasteringVoice *master, std::shared_ptr<const Clip> clip, bool loop)
        : clip_(std::move(clip)), master_(master) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = clip_->channels;
        format.nSamplesPerSec = clip_->sample_rate;
        format.wBitsPerSample = clip_->bits;
        format.nBlockAlign = WORD(format.nChannels * (format.wBitsPerSample / 8));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        IXAudio2SourceVoice *source{};
        check(engine->CreateSourceVoice(&source, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                        nullptr, nullptr, nullptr), "CreateSourceVoice");
        voice_.reset(source);
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(clip_->samples.size());
        buffer.pAudioData = clip_->samples.data();
        if (loop) {
            buffer.LoopLength = buffer.AudioBytes / format.nBlockAlign;
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        }
        check(voice_->SubmitSourceBuffer(&buffer, nullptr), "SubmitSourceBuffer");
        pan(0.0f);
    }

    void play() override {
        if (!started_) {
            check(voice_->Start(0, XAUDIO2_COMMIT_NOW), "Start audio voice");
            started_ = true;
        }
    }
    bool playing() const override {
        XAUDIO2_VOICE_STATE state{};
        voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return started_ && state.BuffersQueued != 0;
    }
    void volume(float value) override { check(voice_->SetVolume(value, XAUDIO2_COMMIT_NOW), "Set audio volume"); }
    void pan(float value) override {
        const float left = value > 0.0f ? 1.0f - value : 1.0f;
        const float right = value < 0.0f ? 1.0f + value : 1.0f;
        std::array<float, 4> matrix{left, right, 0.0f, 0.0f};
        if (clip_->channels == 2) matrix = {left, 0.0f, 0.0f, right};
        check(voice_->SetOutputMatrix(master_, clip_->channels, 2, matrix.data(), XAUDIO2_COMMIT_NOW), "Set audio balance");
    }
};

class XDevice final : public Device {
    ComScope com_;
    Library library_;
    std::unique_ptr<IXAudio2, ReleaseEngine> engine_;
    std::unique_ptr<IXAudio2MasteringVoice, DestroyVoice> master_;

public:
    XDevice() {
        using Create = HRESULT (WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
        const auto address = GetProcAddress(library_.module, "XAudio2Create");
        if (!address) throw std::runtime_error("XAudio2Create is unavailable");
        const auto create = std::bit_cast<Create>(address);
        IXAudio2 *engine{};
        check(create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create");
        engine_.reset(engine);
        IXAudio2MasteringVoice *master{};
        check(engine_->CreateMasteringVoice(&master, 2, XAUDIO2_DEFAULT_SAMPLERATE, 0,
                                            nullptr, nullptr, AudioCategory_GameEffects), "CreateMasteringVoice");
        master_.reset(master);
    }
    std::unique_ptr<Voice> create(std::shared_ptr<const Clip> clip, bool loop) override {
        return std::make_unique<XVoice>(engine_.get(), master_.get(), std::move(clip), loop);
    }
};
} // namespace

std::unique_ptr<Device> create_xaudio_device() { return std::make_unique<XDevice>(); }
} // namespace Audio
