// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "Audio.hpp"
#include "audio_fixture.hpp"

#include <vorbis/vorbisenc.h>

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
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

void master_volume() {
    Fixture f; const auto a = f.service.create(L"Sound.A", true); const auto b = f.service.create(L"Sound.B", false);
    f.service.volume(a, 0.5f); f.service.master_volume(0.5f);
    MG_CHECK(f.device->voices[0]->volume == 0.25f && f.device->voices[1]->volume == 0.5f);
    MG_CHECK(f.service.volume(a) == 0.5f && f.service.volume(b) == 1 && f.service.master_volume() == 0.5f);
    const auto c = f.service.create(L"Sound.A", false); MG_CHECK(f.device->voices[2]->volume == 0.5f);
    f.service.volume(c, 0.2f); MG_CHECK(f.device->voices[2]->volume == 0.1f && f.service.volume(c) == 0.2f);
    f.service.master_volume(std::numeric_limits<float>::quiet_NaN());
    MG_CHECK(f.service.master_volume() == 0 && f.device->voices[0]->volume == 0 && f.device->voices[1]->volume == 0);
    f.service.master_volume(3); MG_CHECK(f.service.master_volume() == 1 && f.device->voices[0]->volume == 0.5f);
    f.device->voices[1]->fail_volume = true; f.service.master_volume(0.5f);
    MG_CHECK(!f.service.playing(b) && f.device->voices[1]->destroyed == 1 && f.reports == 1);
    MG_CHECK(f.device->voices[0]->volume == 0.25f && f.device->voices[2]->volume == 0.1f);
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


// Encodes a 440 Hz tone at half amplitude with the pinned libvorbisenc.
std::vector<uint8_t> vorbis_tone(int channels, long rate, size_t frames) {
    vorbis_info info;
    vorbis_info_init(&info);
    MG_CHECK(vorbis_encode_init_vbr(&info, channels, rate, 0.4f) == 0);
    vorbis_comment comment;
    vorbis_comment_init(&comment);
    vorbis_dsp_state dsp;
    vorbis_analysis_init(&dsp, &info);
    vorbis_block block;
    vorbis_block_init(&dsp, &block);
    ogg_stream_state stream;
    ogg_stream_init(&stream, 1);
    ogg_packet header, comments, codebooks;
    vorbis_analysis_headerout(&dsp, &comment, &header, &comments, &codebooks);
    ogg_stream_packetin(&stream, &header);
    ogg_stream_packetin(&stream, &comments);
    ogg_stream_packetin(&stream, &codebooks);
    std::vector<uint8_t> bytes;
    ogg_page page;
    const auto append = [&] {
        bytes.insert(bytes.end(), page.header, page.header + page.header_len);
        bytes.insert(bytes.end(), page.body, page.body + page.body_len);
    };
    while (ogg_stream_flush(&stream, &page)) append();
    for (size_t done = 0;;) {
        const size_t count = std::min<size_t>(1024, frames - done);
        if (count != 0) {
            float **buffer = vorbis_analysis_buffer(&dsp, int(count));
            for (size_t i = 0; i < count; ++i) {
                for (int c = 0; c < channels; ++c)
                    buffer[c][i] = 0.5f * std::sin(6.2831853f * 440.0f * float(done + i) / float(rate) + float(c));
            }
        }
        vorbis_analysis_wrote(&dsp, int(count));
        done += count;
        while (vorbis_analysis_blockout(&dsp, &block) == 1) {
            vorbis_analysis(&block, nullptr);
            vorbis_bitrate_addblock(&block);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&dsp, &packet)) {
                ogg_stream_packetin(&stream, &packet);
                while (ogg_stream_pageout(&stream, &page)) append();
            }
        }
        if (count == 0) break;
    }
    while (ogg_stream_flush(&stream, &page)) append();
    ogg_stream_clear(&stream);
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);
    return bytes;
}

// Decodes a whole track in reads of the given size; returns the frame count and peak level.
std::pair<size_t, int> decode_all(Audio::Decoder &decoder, size_t read_size) {
    std::vector<int16_t> samples(read_size);
    size_t frames = 0;
    int peak = 0;
    while (const size_t count = decoder.read(samples)) {
        MG_CHECK(count <= samples.size() && count % decoder.channels() == 0);
        for (size_t i = 0; i < count; ++i) peak = std::max(peak, std::abs(int(samples[i])));
        frames += count / decoder.channels();
    }
    MG_CHECK(decoder.read(samples) == 0);  // The end stays the end.
    return {frames, peak};
}

void vorbis_decode() {
    for (const int channels : {1, 2}) {
        for (const long rate : {22050L, 44100L}) {
            const size_t frames = size_t(rate) / 2 + 77;
            auto decoder = Audio::open_vorbis(vorbis_tone(channels, rate, frames));
            MG_CHECK(decoder->channels() == channels && decoder->sample_rate() == uint32_t(rate));
            // An odd read size still yields whole frames; the stream keeps its exact length.
            const auto [decoded, peak] = decode_all(*decoder, 1001);
            MG_CHECK(decoded == frames && peak > 12000 && peak < 20000);
        }
    }
}

void reject_track(std::vector<uint8_t> bytes) {
    bool rejected = false;
    try { (void)Audio::open_vorbis(std::move(bytes)); } catch (const std::runtime_error &) { rejected = true; }
    MG_CHECK(rejected);
}

void vorbis_rejects() {
    reject_track({});
    reject_track(wave());  // The engine's WAV clips are not music tracks.
    const auto tone = vorbis_tone(2, 22050, 22050);
    for (const size_t length : {size_t(4), size_t(27), size_t(58), size_t(200)})
        reject_track({tone.begin(), tone.begin() + length});
    auto foreign = tone;
    foreign[29] ^= 0xff;  // The first packet no longer identifies a Vorbis stream.
    reject_track(foreign);
    // Damaged audio pages end or shorten the track without reading outside the data.
    for (const size_t cut : {tone.size() / 2, tone.size() - 1}) {
        try {
            auto decoder = Audio::open_vorbis({tone.begin(), tone.begin() + cut});
            MG_CHECK(decode_all(*decoder, 4096).first < 22050);
        } catch (const std::runtime_error &) {}
    }
    auto corrupt = tone;
    for (size_t i = corrupt.size() / 2; i < corrupt.size() / 2 + 64; ++i) corrupt[i] ^= 0x5a;
    try {
        auto decoder = Audio::open_vorbis(corrupt);
        MG_CHECK(decode_all(*decoder, 4096).first < 22050);
    } catch (const std::runtime_error &) {}
}

struct MusicFixture {
    std::shared_ptr<DeviceState> device = std::make_shared<DeviceState>();
    FakeDevice fake{device};
    std::map<std::wstring, TrackSpec> specs;
    std::set<std::wstring> missing;
    std::set<std::wstring> unsupported;
    std::vector<std::wstring> reports;

    // Track bytes carry the track name, so the decoder factory can find its synthetic content.
    std::unique_ptr<Audio::Music> make(std::vector<std::wstring> tracks, size_t first = 0) {
        return std::make_unique<Audio::Music>(fake, std::move(tracks), first,
            [this](const std::wstring &name) {
                if (missing.contains(name)) throw std::runtime_error("Synthetic missing track");
                std::vector<uint8_t> bytes;
                for (wchar_t ch : name) bytes.push_back(uint8_t(ch));
                return bytes;
            },
            [this](std::vector<uint8_t> bytes) -> std::unique_ptr<Audio::Decoder> {
                const std::wstring name(bytes.begin(), bytes.end());
                if (unsupported.contains(name)) throw std::runtime_error("Synthetic unsupported track");
                return std::make_unique<FakeDecoder>(specs.at(name));
            },
            [this](const std::wstring &name, const std::string &) { reports.push_back(name); });
    }
};

void music_queue() {
    MusicFixture f; f.specs[L"a"] = {1, 8000, 9000, 0};
    auto music = f.make({L"a"});
    MG_CHECK(f.device->streams.empty() && music->active());  // Nothing plays before the first update.
    music->update();
    auto &s = *f.device->streams.at(0);
    MG_CHECK(s.channels == 1 && s.sample_rate == 8000 && s.plays == 1 && s.buffers.size() == Audio::Music::queue_target);
    for (const auto &buffer : s.buffers) MG_CHECK(buffer.size() == 2000);  // 250 ms at 8 kHz.
    MG_CHECK(s.buffers[3].front() == 6000 && s.buffers[3].back() == 7999);
    music->update();
    MG_CHECK(s.buffers.size() == 4);  // A full queue decodes nothing more.
    s.queued = 2;
    music->update();
    // The partial final buffer is followed by the same track from its start.
    MG_CHECK(s.buffers.size() == 6 && s.buffers[4].size() == 1000 && s.buffers[4].front() == 8000);
    MG_CHECK(s.buffers[5].size() == 2000 && s.buffers[5].front() == 0);
    MG_CHECK(s.plays == 1 && f.device->streams.size() == 1 && f.reports.empty() && music->next_track() == 0);
    // A voice that plays each tiny buffer at once still receives at most one queue per update.
    MusicFixture g; g.specs[L"a"] = {1, 8000, 1, 0};
    g.device->drain_streams = true;
    auto tiny = g.make({L"a"});
    tiny->update();
    MG_CHECK(g.device->streams.at(0)->buffers.size() == 4 && g.device->streams[0]->queued == 0);
    tiny->update();
    MG_CHECK(g.device->streams[0]->buffers.size() == 8 && g.device->streams[0]->buffers[7].size() == 1);
}

void music_order() {
    MusicFixture f;
    f.specs[L"a"] = {1, 8000, 2000, 0}; f.specs[L"b"] = {1, 8000, 2000, 10000}; f.specs[L"c"] = {1, 8000, 2000, 20000};
    MG_CHECK(f.make({L"a", L"b", L"c"}, 4)->next_track() == 1);  // The start index wraps; nothing played yet.
    auto music = f.make({L"a", L"b", L"c"}, 1);
    music->update();
    const auto &s = *f.device->streams.at(0);
    MG_CHECK(s.buffers.size() == 4 && s.buffers[0].front() == 10000 && s.buffers[1].front() == 20000);
    MG_CHECK(s.buffers[2].front() == 0 && s.buffers[3].front() == 10000);
    MG_CHECK(music->next_track() == 2);  // A later session starts after the track still playing.
}

void music_failures() {
    MusicFixture f;
    f.missing.insert(L"missing"); f.unsupported.insert(L"unsupported");
    f.specs[L"empty"] = {1, 8000, 0, 0};
    f.specs[L"corrupt"] = {1, 8000, 10000, 100, 2000};
    f.specs[L"good"] = {1, 8000, 2000, 500};
    auto music = f.make({L"missing", L"unsupported", L"empty", L"corrupt", L"good"});
    music->update();
    const auto &s = *f.device->streams.at(0);
    MG_CHECK(f.reports == std::vector<std::wstring>({L"missing", L"unsupported", L"empty", L"corrupt"}));
    MG_CHECK(s.buffers.size() == 4 && s.buffers[0].front() == 100 && s.buffers[1].front() == 500);
    MG_CHECK(s.buffers[2].front() == 500 && s.buffers[3].front() == 500);
    f.device->streams[0]->queued = 0;
    music->update();  // Failed tracks are skipped without further reports.
    MG_CHECK(s.buffers.size() == 8 && s.buffers[7].front() == 500 && f.reports.size() == 4 && music->active());

    MusicFixture none;
    none.missing.insert(L"missing"); none.unsupported.insert(L"unsupported");
    auto silent = none.make({L"unsupported", L"missing"});
    silent->update();
    silent->update();
    MG_CHECK(!silent->active() && none.device->streams.empty());
    MG_CHECK(none.reports == std::vector<std::wstring>({L"unsupported", L"missing"}));
    MusicFixture invalid; invalid.specs[L"wide"] = {6, 8000, 2000, 0}; invalid.specs[L"slow"] = {1, 999, 2000, 0};
    auto rejected = invalid.make({L"wide", L"slow"});
    rejected->update();
    MG_CHECK(!rejected->active() && invalid.reports.size() == 2 && invalid.device->streams.empty());
    MG_CHECK(!f.make({})->active());
}

void music_format_change() {
    MusicFixture f; f.specs[L"mono"] = {1, 8000, 2000, 0}; f.specs[L"stereo"] = {2, 16000, 4000, 7};
    auto music = f.make({L"mono", L"stereo"});
    music->volume(0.5f);
    music->update();
    // The stereo track waits until the mono voice has played its queue.
    MG_CHECK(f.device->streams.size() == 1 && f.device->streams[0]->buffers.size() == 1);
    music->update();
    MG_CHECK(f.device->streams.size() == 1);
    f.device->streams[0]->queued = 0;
    music->update();
    MG_CHECK(f.device->streams.size() == 2 && f.device->streams[0]->destroyed);
    const auto &stereo = *f.device->streams[1];
    MG_CHECK(stereo.channels == 2 && stereo.sample_rate == 16000 && stereo.plays == 1 && stereo.volume == 0.5f);
    MG_CHECK(stereo.buffers.size() == 1 && stereo.buffers[0].size() == 8000 && stereo.buffers[0].front() == 7);
}

void music_volume() {
    MusicFixture f; f.specs[L"a"] = {1, 8000, 9000, 0};
    auto music = f.make({L"a"});
    music->volume(0.5f);
    music->update();
    auto &s = *f.device->streams.at(0);
    MG_CHECK(s.volume == 0.5f);
    music->level(0.5f);
    MG_CHECK(s.volume == 0.25f && music->level() == 0.5f && music->volume() == 0.5f);
    music->volume(std::numeric_limits<float>::quiet_NaN());
    MG_CHECK(music->volume() == 0 && s.volume == 0);
    music->volume(3);
    MG_CHECK(music->volume() == 1 && s.volume == 0.5f);
    music->level(-1);
    MG_CHECK(music->level() == 0 && s.volume == 0);
}

void music_device_failure() {
    MusicFixture f; f.specs[L"a"] = {1, 8000, 9000, 0};
    f.device->fail_stream = true;
    auto music = f.make({L"a"});
    music->update();
    music->update();
    MG_CHECK(!music->active() && f.reports == std::vector<std::wstring>({L"music"}));
    for (const auto failure : {&StreamState::fail_submit, &StreamState::fail_volume}) {
        MusicFixture g; g.specs[L"a"] = {1, 8000, 9000, 0};
        auto player = g.make({L"a"});
        player->update();
        auto &stream = *g.device->streams.at(0);
        stream.*failure = true;
        stream.queued = 0;
        player->update();
        player->volume(0.5f);
        MG_CHECK(!player->active() && stream.destroyed && g.reports == std::vector<std::wstring>({L"music"}));
    }
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
    {"game.audio.master_volume", master_volume},
    {"game.audio.missing_data", missing_data}, {"game.audio.device_failure", device_failure},
    {"game.audio.shutdown_order", shutdown_order},
    {"game.audio.music_queue", music_queue}, {"game.audio.music_order", music_order},
    {"game.audio.music_failures", music_failures}, {"game.audio.music_format_change", music_format_change},
    {"game.audio.music_volume", music_volume}, {"game.audio.music_device_failure", music_device_failure},
    {"game.audio.vorbis_decode", vorbis_decode}, {"game.audio.vorbis_rejects", vorbis_rejects},
};
} // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
