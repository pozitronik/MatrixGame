// MatrixGame - licensed under GPLv2 or any later version.
#include "StandaloneSettings.hpp"
#include "RandomConfiguration.hpp"

#include "CBlockPar.hpp"
#include "CException.hpp"
#include "utils.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace StandaloneSettings {
namespace {

constexpr std::array keys{L"RandomGenerator", L"ResolutionWidth", L"ResolutionHeight", L"WindowMode",
                          L"FullscreenMode",  L"SoundVolume",     L"StencilShadows",   L"ProjectedShadows",
                          L"Skybox",          L"RobotShadows",    L"CameraSpeed"};

std::optional<std::wstring> single(const Base::CBlockPar &options, const wchar_t *key) {
    if (options.ParCount(key) != 1) return std::nullopt;
    auto value = utils::trim(options.ParGet(key));
    utils::to_lower(value);
    return value;
}

std::optional<int> percent(const std::wstring &value, int minimum, int maximum) {
    if (value.empty() || value.size() > 3) return std::nullopt;
    int number = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return std::nullopt;
        number = number * 10 + (ch - L'0');
    }
    if (number < minimum || number > maximum) return std::nullopt;
    return number;
}

std::optional<bool> boolean(const std::wstring &value) {
    if (value == L"on" || value == L"true" || value == L"1") return true;
    if (value == L"off" || value == L"false" || value == L"0") return false;
    return std::nullopt;
}

std::optional<Skybox> skybox(const std::wstring &value) {
    if (value == L"off") return Skybox::Off;
    if (value == L"low") return Skybox::Low;
    if (value == L"high") return Skybox::High;
    return std::nullopt;
}

// Reads an optional key; an absent key keeps the default, a present invalid key is reported.
template<class T, class Parse>
void read(const Base::CBlockPar &options, const wchar_t *key, T &target, Parse parse, Selection &selection) {
    if (!options.ParCount(key)) return;
    const auto value = single(options, key);
    const auto parsed = value ? parse(*value) : decltype(parse(std::wstring{})){};
    if (parsed) target = *parsed;
    else selection.invalid.push_back(utils::from_wstring(key));
}

std::wstring value_text(const Values &values, std::wstring_view key) {
    const auto on_off = [](bool value) { return std::wstring(value ? L"On" : L"Off"); };
    if (key == L"RandomGenerator") return utils::to_wstring(RandomConfiguration::name(values.random));
    if (key == L"ResolutionWidth") return std::to_wstring(values.display.width);
    if (key == L"ResolutionHeight") return std::to_wstring(values.display.height);
    if (key == L"WindowMode") return utils::to_wstring(Display::name(values.display.mode));
    if (key == L"FullscreenMode") return utils::to_wstring(Display::name(values.display.fullscreen));
    if (key == L"SoundVolume") return std::to_wstring(values.sound_volume);
    if (key == L"StencilShadows") return on_off(values.stencil_shadows);
    if (key == L"ProjectedShadows") return on_off(values.projected_shadows);
    if (key == L"Skybox") return utils::to_wstring(name(values.skybox));
    if (key == L"RobotShadows") return on_off(values.robot_shadows);
    if (key == L"CameraSpeed") return std::to_wstring(values.camera_speed);
    throw std::logic_error("Unknown settings key");
}

bool space(wchar_t ch) { return ch == L' ' || ch == L'\t'; }

std::string last_error(const char *operation) {
    return utils::format("%s (Windows error %lu)", operation, GetLastError());
}

}  // namespace

const char *name(Skybox skybox) {
    switch (skybox) {
        case Skybox::Off: return "Off";
        case Skybox::Low: return "Low";
        case Skybox::High: return "High";
    }
    return "High";
}

Selection resolve(const Base::CBlockPar &options) {
    Selection selection;
    auto &values = selection.values;
    // Display and random keys keep their established all-or-default contracts and diagnostics.
    values.display = Display::resolve(options).settings;
    values.random = RandomConfiguration::resolve(options).mode;
    read(options, L"SoundVolume", values.sound_volume,
         [](const std::wstring &value) { return percent(value, 0, 100); }, selection);
    read(options, L"CameraSpeed", values.camera_speed,
         [](const std::wstring &value) { return percent(value, min_camera_speed, max_camera_speed); }, selection);
    read(options, L"StencilShadows", values.stencil_shadows, boolean, selection);
    read(options, L"ProjectedShadows", values.projected_shadows, boolean, selection);
    read(options, L"Skybox", values.skybox, skybox, selection);
    read(options, L"RobotShadows", values.robot_shadows, boolean, selection);
    return selection;
}

Selection load(const wchar_t *path) {
    try {
        Base::CBlockPar options;
        if (std::filesystem::exists(path)) options.LoadFromTextFile(path);
        return resolve(options);
    }
    catch (const Base::CException &) {
    }
    catch (const std::exception &) {
    }
    Selection selection;
    selection.invalid.push_back("CFG/standalone.txt");
    return selection;
}

std::wstring update_text(const std::wstring &text, const Values &values) {
    const std::wstring newline = text.find(L"\r\n") != std::wstring::npos || text.find(L'\n') == std::wstring::npos
                                         ? L"\r\n"
                                         : L"\n";
    std::array<bool, keys.size()> written{};
    std::wstring result;
    int depth = 0;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        const size_t next = end == std::wstring::npos ? text.size() : end + 1;
        if (end == std::wstring::npos) end = text.size();
        size_t content_end = end;
        if (content_end > start && text[content_end - 1] == L'\r') --content_end;
        const std::wstring line = text.substr(start, content_end - start);
        const std::wstring terminator = text.substr(content_end, next - content_end);
        start = next;

        const size_t comment = std::min(line.find(L"//"), line.size());
        const std::wstring code = line.substr(0, comment);
        const auto opens = std::ranges::count(code, L'{');
        const auto closes = std::ranges::count(code, L'}');
        const size_t equals = code.find(L'=');
        if (depth == 0 && opens == 0 && closes == 0 && equals != std::wstring::npos) {
            const std::wstring key = utils::trim(code.substr(0, equals));
            const auto found = std::ranges::find(keys, key);
            if (found != keys.end()) {
                const auto index = static_cast<size_t>(std::distance(keys.begin(), found));
                if (written[index]) continue;  // CBlockPar rejects repeated keys; keep the first.
                written[index] = true;
                size_t value_begin = equals + 1;
                while (value_begin < comment && space(line[value_begin])) ++value_begin;
                size_t value_end = comment;
                while (value_end > value_begin && space(line[value_end - 1])) --value_end;
                result += line.substr(0, value_begin) + value_text(values, key) + line.substr(value_end) + terminator;
                continue;
            }
        }
        depth = std::max(0, depth + static_cast<int>(opens - closes));
        result += line + terminator;
    }
    if (!result.empty() && result.back() != L'\n') result += newline;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!written[i]) result += std::wstring(keys[i]) + L"=" + value_text(values, keys[i]) + newline;
    }
    return result;
}

bool save(const Values &values, std::string &error, const wchar_t *path) {
    const std::filesystem::path target(path);
    auto temporary = target;
    temporary += L".tmp";
    try {
        std::string bytes;
        if (std::filesystem::exists(target)) {
            std::ifstream input(target, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            if (!input.good() && !input.eof()) throw std::runtime_error("Cannot read the current settings file");
        }
        // Match CBlockPar: an even-sized file with a UTF-16LE BOM is wide text, anything else is
        // UTF-8. Supported keys and values are ASCII, so other bytes pass through unchanged.
        const bool wide = bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes[0] == '\xff' && bytes[1] == '\xfe';
        std::wstring text;
        if (wide) {
            for (size_t i = 2; i + 1 < bytes.size(); i += 2)
                text += static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) |
                                             (static_cast<unsigned char>(bytes[i + 1]) << 8));
        }
        else {
            for (char ch : bytes) text += static_cast<wchar_t>(static_cast<unsigned char>(ch));
        }
        const std::wstring updated = update_text(text, values);
        std::string output = wide ? std::string("\xff\xfe") : std::string{};
        for (wchar_t ch : updated) {
            output += static_cast<char>(ch & 0xff);
            if (wide) output += static_cast<char>((ch >> 8) & 0xff);
        }

        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(output.data(), static_cast<std::streamsize>(output.size()));
            stream.flush();
            if (!stream.good()) throw std::runtime_error("Cannot write a temporary settings file");
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error(last_error("Cannot replace the settings file"));
        return true;
    }
    catch (const std::exception &failure) {
        error = failure.what();
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
}

}  // namespace StandaloneSettings
