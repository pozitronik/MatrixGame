// MatrixGame - licensed under GPLv2 or any later version.
#include "SoundBridge.hpp"
#include "MatrixGameDll.hpp"
#include "CFile.hpp"
#include "CBlockPar.hpp"
#include <stupid_logger.hpp>
#include <utils.hpp>
#include <stdexcept>

namespace SoundBridge {
namespace {
std::unique_ptr<Audio::Service> standalone;
std::vector<uint8_t> load(const std::wstring &path) {
    try {
        Base::CFile file(path);
        file.OpenRead();
        const auto length = file.Size();
        if (length == 0 || length > 16 * 1024 * 1024)
            throw std::runtime_error("Sound resource has an invalid size");
        std::vector<uint8_t> bytes(length);
        file.Read(bytes.data(), length);
        return bytes;
    } catch (const Base::CException &) {
        throw std::runtime_error("Cannot read sound resource");
    }
}
} // namespace

bool available() { return g_RangersInterface != nullptr || standalone != nullptr; }
void install(std::unique_ptr<Audio::Service> service) { standalone = std::move(service); }
void shutdown() { standalone.reset(); }

void initialize_standalone() {
    shutdown();
    try {
        Base::CBlockPar config;
        config.LoadFromTextFile(L"CFG\\sounds.txt");
        auto *sounds = config.BlockGetNE(L"Sound");
        if (!sounds) throw std::runtime_error("Sound mapping needs a Sound block");
        Audio::Mapping mapping;
        for (int i = 0; i < sounds->ParCount(); ++i)
            mapping.emplace(L"Sound." + sounds->ParGetName(i), sounds->ParGet(i));
        install(std::make_unique<Audio::Service>(Audio::create_xaudio_device(), std::move(mapping), load,
            [](const std::wstring &name, const std::string &message) {
                lgr.error("Audio {}: {}")(utils::from_wstring(name), message);
            }));
        lgr.info("Standalone sound initialized");
    } catch (const Base::CException &) {
        lgr.error("Standalone sound disabled: prepare CFG/sounds.txt and local sound resources");
    } catch (const std::exception &error) {
        lgr.error("Standalone sound disabled: {}")(error.what());
    }
}

uint32_t create(wchar_t *name, int group, int loop) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundCreate(name, group, loop);
    if (!standalone) return 0;
    const auto id = standalone->create(name, loop != 0);
    return id == Audio::invalid_id ? 0 : id;
}
void destroy(uint32_t id) {
    if (g_RangersInterface) g_RangersInterface->m_SoundDestroy(id);
    else if (standalone) standalone->destroy(id);
}
void play(uint32_t id) {
    if (g_RangersInterface) g_RangersInterface->m_SoundPlay(id);
    else if (standalone) standalone->play(id);
}
bool playing(uint32_t id) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundIsPlay(id) != 0;
    return standalone && standalone->playing(id);
}
void volume(uint32_t id, float value) {
    if (g_RangersInterface) g_RangersInterface->m_SoundVolume(id, value);
    else if (standalone) standalone->volume(id, value);
}
void pan(uint32_t id, float value) {
    if (g_RangersInterface) g_RangersInterface->m_SoundPan(id, value);
    else if (standalone) standalone->pan(id, value);
}
float volume(uint32_t id) {
    if (g_RangersInterface) return g_RangersInterface->m_SoundGetVolume(id);
    return standalone ? standalone->volume(id) : 0.0f;
}
} // namespace SoundBridge
