// MatrixGame - licensed under GPLv2 or any later version.
#include "MapSelection.hpp"
#include "MapCatalog.hpp"
#include "ExecutablePath.hpp"

#include "utils.hpp"

#include <windows.h>

#include <cwctype>

#include <stupid_logger.hpp>

namespace MapSelection {
namespace {
bool remembered_started = false;
bool map_loading = false;
std::wstring remembered;
bool relaunch_pending = false;

bool store_map(const std::wstring &map) {
    auto stored = StandaloneSettings::load().values;
    stored.map = map;
    std::string error;
    if (StandaloneSettings::save(stored, error)) return true;
    lgr.error("Cannot save CFG/standalone.txt: {}")(error);
    return false;
}
}  // namespace

bool is_demo(const std::wstring &path) {
    std::wstring lower = path;
    for (auto &ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    return lower.find(L"demo") != std::wstring::npos;
}

std::wstring startup_map(StandaloneSettings::Values &values) {
    if (values.map.empty()) return {};
    if (MapCatalog::readable(values.map)) {
        remembered_started = true;
        remembered = values.map;
        lgr.notice("Starting the remembered map {}")(utils::from_wstring(values.map));
        return values.map;
    }
    lgr.error("The remembered map {} cannot be read; starting the configured map")(utils::from_wstring(values.map));
    const std::wstring message = L"The map " + values.map +
                                 L" chosen in the Map window cannot be loaded.\nThe game starts the configured map "
                                 L"instead. Choose another map in Settings, Map.";
    values.map.clear();
    auto stored = StandaloneSettings::load().values;
    stored.map.clear();
    std::string error;
    if (!StandaloneSettings::save(stored, error)) lgr.error("Cannot save CFG/standalone.txt: {}")(error);
    MessageBoxW(nullptr, message.c_str(), L"Map unavailable", MB_OK | MB_ICONWARNING);
    return {};
}

bool remembered_map_started() { return remembered_started; }

void begin_map_load() {
    if (!remembered_started) return;
    map_loading = true;
    store_map({});
}

void finish_map_load() {
    if (!map_loading) return;
    map_loading = false;
    if (is_demo(remembered)) {
        lgr.notice("The demo map {} plays once; the next start uses the configured map")(utils::from_wstring(remembered));
        return;
    }
    store_map(remembered);
}

bool forget_remembered_map() {
    if (!map_loading) return false;
    map_loading = false;
    store_map({});  // Already cleared before loading; repeated in case that save failed.
    lgr.notice("Forgot the remembered map after a failed start");
    return true;
}

bool choose(StandaloneSettings::Values values, const std::wstring &map, std::string &error) {
    values.map = map;
    if (!StandaloneSettings::save(values, error)) return false;
    lgr.notice("Remembered map {}; restarting the game")(utils::from_wstring(map));
    relaunch_pending = true;
    return true;
}

bool relaunch_requested() { return relaunch_pending; }

bool relaunch() {
    const auto executable = Startup::ExecutablePath();
    std::wstring command = L"\"" + executable.native() + L"\"";
    const std::wstring directory = executable.parent_path().native();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                        &startup, &process)) {
        lgr.error("Cannot start the game again (Windows error {})")(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace MapSelection
