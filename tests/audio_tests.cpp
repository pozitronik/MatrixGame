// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "Audio.hpp"
#include "audio_fixture.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace {
using namespace audio_fixture;

void pcm_formats() {
    for (const uint16_t channels : {uint16_t(1), uint16_t(2)}) {
        for (const uint16_t bits : {uint16_t(8), uint16_t(16)}) {
            const auto bytes = wave(channels, bits, 22255, true);
            const auto clip = Audio::read_wave(bytes);
            MG_CHECK(clip.channels == channels && clip.bits == bits && clip.sample_rate == 22255);
            MG_CHECK(clip.samples == std::vector<uint8_t>({0, 1, 2, 3, 4, 5, 6, 7}));
        }
    }
}

void reject(std::vector<uint8_t> bytes) {
    bool rejected = false;
    try { (void)Audio::read_wave(bytes); } catch (const std::runtime_error &) { rejected = true; }
    MG_CHECK(rejected);
}

void malformed_wave() {
    const auto original = wave();
    for (size_t length = 0; length < original.size(); ++length) reject({original.begin(), original.begin() + length});
    auto bytes = original; bytes[20] = 3; reject(bytes); // Floating-point/other encoding.
    bytes = original; bytes[22] = 0; reject(bytes); // Zero channels.
    bytes = original; bytes[32] = 8; reject(bytes); // Wrong frame width.
    bytes = original; bytes[28] = 0; reject(bytes); // Wrong byte rate.
    bytes = original; bytes[40] = 7; reject(bytes); // Partial PCM frame.
    bytes = original; bytes[4] = 0xff; reject(bytes); // Oversized RIFF length.
}

void legacy_wave_headers() {
    auto bytes = wave(1, 8);
    const auto size = static_cast<uint32_t>(bytes.size());
    for (size_t i = 0; i < 4; ++i) bytes[4 + i] = uint8_t(size >> (8 * i));
    MG_CHECK(Audio::read_wave(bytes).samples.size() == 8);
    bytes = wave(1, 8);
    bytes.pop_back(); bytes[4] = uint8_t(bytes.size() - 8); bytes[40] = 7;
    MG_CHECK(Audio::read_wave(bytes).samples.size() == 7);
    bytes.push_back(0); // Pad physically present but not counted by the RIFF header.
    MG_CHECK(Audio::read_wave(bytes).samples.size() == 7);
    bytes = wave(1, 8, 22255, true);
    bytes.erase(bytes.begin() + 21); // Missing intermediate JUNK padding stays invalid.
    reject(bytes);
}


void handles_and_cache() {
    Fixture f;
    const auto a = f.service.create(L"Sound.A", true); const auto b = f.service.create(L"Sound.B", false);
    MG_CHECK(a != Audio::invalid_id && b != Audio::invalid_id && a != b);
    MG_CHECK(f.loads == 1 && f.device->voices[0]->loop && !f.device->voices[1]->loop);
    MG_CHECK(f.device->voices[0]->clip.lock() == f.device->voices[1]->clip.lock());
    f.service.play(a); f.service.play(b); MG_CHECK(f.service.playing(a) && f.service.playing(b));
    f.service.destroy(a); f.service.play(a);
    MG_CHECK(!f.service.playing(a) && f.service.playing(b) && f.device->voices[0]->destroyed == 1);
    f.service.clear(); MG_CHECK(!f.service.playing(b));
    const auto c = f.service.create(L"Sound.A", false); MG_CHECK(c != a && c != b);
}

void parameters() {
    Fixture f; const auto id = f.service.create(L"Sound.A", false);
    f.service.volume(id, 7); f.service.pan(id, -7);
    MG_CHECK(f.service.volume(id) == 1 && f.service.pan(id) == -1);
    f.service.volume(id, std::numeric_limits<float>::quiet_NaN()); f.service.pan(id, std::numeric_limits<float>::infinity());
    MG_CHECK(f.service.volume(id) == 0 && f.service.pan(id) == 0);
    f.service.volume(id, 0.4f); f.service.pan(id, 0.25f);
    MG_CHECK(f.device->voices[0]->volume == 0.4f && f.device->voices[0]->pan == 0.25f);
    f.service.destroy(id); MG_CHECK(f.service.volume(id) == 0 && f.service.pan(id) == 0);
}

void missing_data() {
    Fixture f; f.fail_load = true;
    for (int i = 0; i < 8; ++i) MG_CHECK(f.service.create(L"Sound.A", false) == Audio::invalid_id);
    MG_CHECK(f.loads == 1 && f.reports == 1 && f.device->voices.empty());
    MG_CHECK(f.service.create(L"missing", false) == Audio::invalid_id);
    MG_CHECK(f.loads == 1 && f.reports == 2);
    f.service.clear(); f.fail_load = false; MG_CHECK(f.service.create(L"Sound.A", false) != Audio::invalid_id);
}

void device_failure() {
    Fixture f; f.device->fail_create = true;
    MG_CHECK(f.service.create(L"Sound.A", false) == Audio::invalid_id);
    f.service.clear(); f.device->fail_create = false;
    const auto id = f.service.create(L"Sound.A", false); f.device->voices.back()->fail_play = true;
    f.service.play(id); MG_CHECK(!f.service.playing(id));
    MG_CHECK(f.device->voices.back()->destroyed == 1 && f.reports == 2);
}

void shutdown_order() {
    std::shared_ptr<DeviceState> state;
    {
        Fixture f; state = f.device;
        const auto id = f.service.create(L"Sound.A", true); f.service.play(id);
    }
    MG_CHECK(state->destroyed && !state->live_clip_at_shutdown);
    MG_CHECK(state->voices[0]->destroyed == 1 && !state->voices[0]->playing);
}

constexpr tests::Case cases[] = {
    {"manual.audio.native_device", [] {
        Audio::Service service{Audio::create_xaudio_device(), {{L"synthetic", L"synthetic.wav"}},
            [](const std::wstring &) { return wave(); }, {}};
        const auto id = service.create(L"synthetic", true);
        MG_CHECK(id != Audio::invalid_id);
        service.volume(id, 0.0f);
        service.pan(id, -0.5f);
        service.play(id);
        MG_CHECK(service.playing(id));
        service.destroy(id);
        MG_CHECK(!service.playing(id));
    }},
    {"game.audio.pcm_formats", pcm_formats}, {"game.audio.malformed_wave", malformed_wave},
    {"game.audio.legacy_wave_headers", legacy_wave_headers},
    {"game.audio.handles_cache", handles_and_cache}, {"game.audio.parameters", parameters},
    {"game.audio.missing_data", missing_data}, {"game.audio.device_failure", device_failure},
    {"game.audio.shutdown_order", shutdown_order},
};
} // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
