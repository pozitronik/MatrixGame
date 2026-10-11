// MatrixGame - licensed under GPLv2 or any later version.
#include "SoundBridge.hpp"
#include "SoundMapping.hpp"
#include "MatrixGameDll.hpp"
#include "CFile.hpp"
#include "CBlockPar.hpp"
#include <stupid_logger.hpp>
#include <utils.hpp>
#include <stdexcept>
#include <algorithm>
#include <filesystem>

namespace SoundBridge {
namespace {
std::unique_ptr<Audio::Service> standalone;
Audio::Mapping sound_mapping() {
    auto mapping = Audio::default_sound_mapping();
    try {
        if (!std::filesystem::exists(L"CFG\\sounds.txt")) return mapping;
        Base::CBlockPar config;
        config.LoadFromTextFile(L"CFG\\sounds.txt");
        if (config.BlockCount(L"Sound") != 1) throw std::runtime_error("Expected one Sound block");
        const auto *sounds = config.BlockGetNE(L"Sound");
        if (sounds->BlockCount()) throw std::runtime_error("Nested sound mappings are unsupported");
        Audio::Mapping overrides;
        for (int i = 0; i < sounds->ParCount(); ++i) {
            const std::wstring name = sounds->ParGetName(i);
            const std::wstring path = sounds->ParGet(i);
            const bool valid_name = !name.empty() && std::all_of(name.begin(), name.end(), [](wchar_t c) {
                return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                       (c >= L'0' && c <= L'9') || c == L'_';
            });
            if (!valid_name || path.empty()) throw std::runtime_error("Invalid sound name or empty resource path");
            if (!overrides.emplace(L"Sound." + name, path).second)
                throw std::runtime_error("Duplicate sound override");
        }
        // Validate the entire override before changing any default.
        for (auto &[name, path] : overrides) mapping.insert_or_assign(name, std::move(path));
    } catch (const Base::CException &) {
        lgr.warning("Cannot read CFG/sounds.txt; using default sound mappings");
    } catch (const std::exception &error) {
        lgr.warning("Ignoring CFG/sounds.txt; using default sound mappings: {}")(error.what());
    }
    return mapping;
}

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

void initialize_standalone(bool resources_available) {
    shutdown();
    if (!resources_available) return;
    try {
        auto mapping = sound_mapping();
        install(std::make_unique<Audio::Service>(Audio::create_xaudio_device(), std::move(mapping), load,
            [](const std::wstring &name, const std::string &message) {
                lgr.error("Audio {}: {}")(utils::from_wstring(name), message);
            }));
        lgr.notice("Standalone sound initialized");
    } catch (const Base::CException &) {
        lgr.error("Standalone sound disabled: cannot initialize audio resources");
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
