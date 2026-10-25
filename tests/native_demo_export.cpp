// MatrixGame - licensed under GPLv2 or any later version.
#include "native_demo_assets.hpp"
#include "native_calcvis_assets.hpp"
#include "native_session_assets.hpp"
#include "native_packed_config.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "StandaloneSettings.hpp"
#include "CStorage.hpp"
#include "Pack.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <shellapi.h>

namespace fs = std::filesystem;
std::wstring PathToOutputFiles(const wchar_t *destination) {
    const auto path = fs::current_path() / destination;
    fs::create_directories(path);
    return path.wstring();
}
namespace {
constexpr std::array profiles{L"ordinary", L"reinforcement", L"no-reinforcement", L"win", L"loss", L"fights", L"calcvis-flat", L"calcvis-ridge"};
bool calcvis(std::wstring_view profile) { return profile == L"calcvis-flat" || profile == L"calcvis-ridge"; }
bool reinforcement(std::wstring_view profile) { return profile == L"reinforcement" || profile == L"no-reinforcement"; }
fs::path map_path(std::wstring_view profile) {
    return fs::path(L"Matrix/Map") / (profile == L"ordinary" ? L"ordinary.cmap" : (calcvis(profile) ? std::wstring(profile)+L".cmap" : L"demo-" + std::wstring(profile) + L".cmap"));
}
struct Directory {
    fs::path previous = fs::current_path(), root;
    bool closed = false;
    explicit Directory(const fs::path &path) : root(fs::absolute(path).lexically_normal()) {
        MG_CHECK(fs::is_directory(root.parent_path()));
        MG_CHECK(!fs::exists(root) && fs::create_directory(root));
        try { fs::current_path(root); }
        catch (...) { std::error_code error; fs::remove_all(root, error); throw; }
    }
    void close(bool keep = false) {
        fs::current_path(previous);
        if (!keep) fs::remove_all(root);
        closed = true;
    }
    ~Directory() {
        if (closed) return;
        std::error_code error;
        fs::current_path(previous, error);
        if (!error) fs::remove_all(root, error);
        if (error) std::cerr << "Cannot clean synthetic demo export: " << error.message() << '\n';
    }
};
void same_tree(const CBlockPar &left, const CBlockPar &right) {
    MG_CHECK(left.ParCount() == right.ParCount() && left.BlockCount() == right.BlockCount());
    for (int i = 0; i < left.ParCount(); ++i)
        MG_CHECK(left.ParGetName(i) == right.ParGetName(i) && left.ParGet(i) == right.ParGet(i));
    for (int i = 0; i < left.BlockCount(); ++i) {
        MG_CHECK(left.BlockGetName(i) == right.BlockGetName(i));
        same_tree(*left.BlockGet(i), *right.BlockGet(i));
    }
}
void generate(std::wstring_view profile, bool packed) {
    tests::write_native_session_assets();
    fs::create_directories(L"Matrix/Map");
    const auto map = map_path(profile);
    if (calcvis(profile)) { tests::write_native_calcvis_map(map,profile == L"calcvis-ridge"); tests::configure_native_calcvis(); }
    else if (profile == L"ordinary") tests::write_native_session_map(map);
    else if (reinforcement(profile)) tests::write_native_demo_reinforcement(map, profile == L"reinforcement");
    else if (profile == L"fights") tests::write_native_demo_fights(map);
    else tests::write_native_demo_outcome(map, profile == L"win");
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(L"Config")->ParSetAdd(L"Map", map.wstring().c_str());
    if (reinforcement(profile)) {
        // Keep this visible observation's ownership control stable; native helper inputs remain unchanged.
        auto *capture = data.BlockGet(PAR_SOURCE_TIMINGS)->BlockGet(PAR_SOURCE_TIMINGS_CAPTURE);
        capture->ParSetAdd(PAR_SOURCE_TIMINGS_ERASE, L"1000000");
        capture->ParSetAdd(PAR_SOURCE_TIMINGS_PAINT, L"1000000");
    }
    data.SaveInTextFile(L"CFG/robots/data.txt");
    if (packed) tests::pack_native_session_configuration();
    std::ofstream options(L"CFG/standalone.txt");
    options << "RandomGenerator=ParkMiller\nWindowMode=Windowed\nResolutionWidth=1280\nResolutionHeight=720\n"
               "StencilShadows=Off\nProjectedShadows=Off\nRobotShadows=Off\nSkybox=Off\nBattleBonus=None\nMap="
            << map.generic_string() << '\n';
    MG_CHECK(options.good());
    fs::create_directories(L"DATA");
    std::ofstream package(L"DATA/robots.pkg", std::ios::binary);
    for (uint32_t word : {4u, 12u, 0u, 158u})
        for (int shift = 0; shift < 32; shift += 8) package.put(char((word >> shift) & 255));
    MG_CHECK(package.good());
}
template<class T> void column(CStorage &map, const wchar_t *table, const wchar_t *name,
                              EStorageType type, std::initializer_list<T> expected) {
    auto *values = map.GetBuf(table, name, type);
    MG_CHECK(values && values->GetArraysCount() == 1 && values->GetArrayLength(0) == expected.size());
    MG_CHECK(std::equal(expected.begin(), expected.end(), values->GetFirst<T>(0)));
}
void validate(std::wstring_view profile, bool packed, bool staged = false) {
    for (const auto *file : {L"pixel.png", L"synthetic.map", L"CFG/standalone.txt", L"DATA/robots.pkg"})
        MG_CHECK(fs::is_regular_file(file) && fs::file_size(file) > 0);
    MG_CHECK(staged || !fs::exists(L"MatrixGame.exe"));
    MG_CHECK(fs::is_regular_file(map_path(profile)));
    std::ifstream settings_text(L"CFG/standalone.txt");
    MG_CHECK(settings_text.good());
    const std::string written{std::istreambuf_iterator<char>(settings_text), {}};
    for (const auto *expected : {"RandomGenerator=ParkMiller", "WindowMode=Windowed", "BattleBonus=None"}) {
        const auto line = std::string(expected) + '\n';
        const auto offset = written.find(line);
        MG_CHECK(offset != std::string::npos && (offset == 0 || written[offset - 1] == '\n'));
        MG_CHECK(written.find(line, offset + line.size()) == std::string::npos);
    }
    const auto options = StandaloneSettings::load();
    MG_CHECK(options.invalid.empty() && options.values.random == random::Mode::ParkMiller);
    MG_CHECK(options.values.display.mode == Display::Mode::Windowed);
    MG_CHECK(options.values.display.width == 1280 && options.values.display.height == 720);
    MG_CHECK(!options.values.stencil_shadows && !options.values.projected_shadows && !options.values.robot_shadows);
    MG_CHECK(options.values.skybox == StandaloneSettings::Skybox::Off && options.values.battle_bonus == BattleChoice::Bonus::None);
    MG_CHECK(fs::path(options.values.map).lexically_normal() == map_path(profile));
    CBlockPar data, interface_config;
    if (packed) {
        MG_CHECK(fs::is_regular_file(L"CFG/robots.dat") && !fs::exists(L"CFG/robots"));
        CStorage archive;
        MG_CHECK(archive.Load(L"CFG/robots.dat"));
        archive.RestoreBlockPar(L"da", data); archive.RestoreBlockPar(L"if", interface_config);
        CBlockPar text_data, text_interface;
        text_data.LoadFromTextFile(L"parked-text-config/data.txt");
        text_interface.LoadFromTextFile(L"parked-text-config/iface.txt");
        same_tree(data, text_data); same_tree(interface_config, text_interface);
    } else {
        MG_CHECK(!fs::exists(L"CFG/robots.dat") && !fs::exists(L"parked-text-config"));
        data.LoadFromTextFile(L"CFG/robots/data.txt"); interface_config.LoadFromTextFile(L"CFG/robots/iface.txt");
    }
    MG_CHECK(interface_config.BlockCount() > 0);
    MG_CHECK(fs::path(data.BlockGet(L"Config")->ParGet(L"Map").c_str()).lexically_normal() == map_path(profile));
    Base::CPackCollection packages(nullptr);
    packages.AddPacketFile(L"DATA/robots.pkg");
    MG_CHECK(packages.OpenPacketFiles() && packages.m_PackFiles.size() == 1);
    CStorage map;
    MG_CHECK(map.Load(map_path(profile).c_str()));
    const bool fights = profile == L"fights", ordinary = profile == L"ordinary" || reinforcement(profile) || calcvis(profile);
    column<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE, fights ?
        std::initializer_list<BYTE>{1,2,2} : std::initializer_list<BYTE>{1,2});
    column<float>(map, DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT, fights ? std::initializer_list<float>{250,350,400} :
        ordinary ? std::initializer_list<float>{100,500} : std::initializer_list<float>{250,350});
    column<float>(map, DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT, fights ? std::initializer_list<float>{250,250,300} :
        ordinary ? std::initializer_list<float>{100,500} : std::initializer_list<float>{250,250});
    auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR);
    MG_CHECK(units && units->GetArraysCount() == (fights ? 3u : 2u));
    const auto laser = L"|" + std::to_wstring(MRT_WEAPON) + L"," + std::to_wstring(RUK_WEAPON_LASER);
    for (DWORD i = 0; i < units->GetArraysCount(); ++i) {
        const bool armed = ((profile == L"win" || fights) && i == 0) || (profile == L"loss" && i == 1);
        const auto modules = std::to_wstring(MRT_CHASSIS) + L"," + std::to_wstring(RUK_CHASSIS_WHEEL) +
            L",0|" + std::to_wstring(MRT_ARMOR) + L"," + std::to_wstring(fights && i == 2 ? RUK_ARMOR_DIPLOID : RUK_ARMOR_MONOSTACK);
        MG_CHECK(units->GetAsWStr(i) == modules + (armed ? laser : L""));
    }
    column<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE, fights ?
        std::initializer_list<BYTE>{0,0,0} : std::initializer_list<BYTE>{0,0});
    for (const auto *name : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        column<int>(map, DATA_ROBOTS, name, ST_INT32, fights ? std::initializer_list<int>{0,0,0} : std::initializer_list<int>{0,0});
    if (reinforcement(profile)) {
        column<BYTE>(map, DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE, {BYTE(profile == L"reinforcement" ? PLAYER_SIDE : 2)});
        column<BYTE>(map, DATA_BUILDINGS, DATA_BUILDINGS_KIND, ST_BYTE, {BUILDING_TITAN});
        column<float>(map, DATA_BUILDINGS, DATA_BUILDINGS_X, ST_FLOAT, {400});
        column<float>(map, DATA_BUILDINGS, DATA_BUILDINGS_Y, ST_FLOAT, {100});
        auto *names = map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR);
        auto *values = map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR);
        MG_CHECK(names && values);
        const int maintenance = names->FindAsWStr(DATA_MAINTENANCETIME);
        MG_CHECK(maintenance >= 0 && values->GetAsWStr(maintenance) == L"1");
        auto *timings = data.BlockGet(PAR_SOURCE_TIMINGS);
        MG_CHECK(timings->ParGet(PAR_SOURCE_TIMINGS_MAINTENANCE) == L"20000");
        for (const auto *key : {PAR_SOURCE_TIMINGS_ERASE, PAR_SOURCE_TIMINGS_PAINT})
            MG_CHECK(timings->BlockGet(PAR_SOURCE_TIMINGS_CAPTURE)->ParGet(key) == L"1000000");
        for (const auto *key : {PAR_SOURCE_TIMINGS_BASE, PAR_SOURCE_TIMINGS_TITAN})
            MG_CHECK(timings->BlockGet(PAR_SOURCE_TIMINGS_RESOURCES)->ParGet(key) == L"1000000");
        MG_CHECK(fs::is_regular_file(L"synthetic-transport-body.vo") && fs::is_regular_file(L"Matrix/Building/test-building.vo"));
        auto *model = data.BlockGet(L"Models")->BlockGet(L"Flyers")->BlockGet(L"1");
        MG_CHECK(model->BlockCount() == 1 && model->BlockGet(L"Body")->ParGet(L"Model") == L"synthetic-transport-body.vo");
        auto *order = data.BlockGet(PAR_SOURCE_FLYER_ORDERS)->BlockGet(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
        MG_CHECK(order->BlockGet(L"100")->ParGet(L"BotHitpoint") == L"1500");
        MG_CHECK(order->BlockGet(L"100")->ParGet(L"BotChassis") == L"2" && order->BlockGet(L"100")->ParGet(L"BotArmor") == L"1");
    } else {
        auto *buildings = map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE);
        MG_CHECK(buildings && buildings->GetArrayLength(0) == 0);
    }
    if (calcvis(profile)) {
        for(const auto &entry:fs::directory_iterator(L"CFG")){
            const auto name=entry.path().filename().wstring();
            MG_CHECK(name.rfind(L"robots.dat.",0)!=0);
        }
        tests::validate_native_calcvis(map_path(profile),profile == L"calcvis-ridge");
        tests::validate_native_calcvis_configuration(data);
    }
    if (!ordinary)
        MG_CHECK(data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_DAMAGES)->BlockGet(PAR_SOURCE_DAMAGES_ROBOT)
            ->ParGet(L"WEAPON_LASER") == L"1000,0,0");
    if (fights) {
        MG_CHECK(data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Chassis")->ParGet(PAR_SOURCE_CHASSIS2_MOVE_SPEED) == L"0");
        MG_CHECK(data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Armor")->ParGet(PAR_SOURCE_ARMOR6_STRUCTURE) == L"30000");
        MG_CHECK(data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Armor")->ParGet(PAR_SOURCE_ARMOR2_STRUCTURE) == L"200000");
        MG_CHECK(data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_OVERHEAT)
            ->ParGet(PAR_SOURCE_OVERHEAT_WEAPON_LASER_HEAT) == L"0");
    }
}
void export_to(std::wstring_view profile, const fs::path &target, bool packed, void (*after_generate)() = nullptr) {
    MG_CHECK(std::find(profiles.begin(), profiles.end(), profile) != profiles.end());
    Directory destination(target);
    generate(profile, packed);
    if (after_generate) after_generate();
    validate(profile, packed);
    destination.close(true);
}
void export_layout(std::wstring_view profile) {
    Directory fixture("demo-layout-" + std::to_string(GetCurrentProcessId()));
    for (bool packed : {false,true}) {
        const auto destination = fixture.root / (std::wstring(profile) + (packed ? L"-packed" : L"-text"));
        export_to(profile, destination, packed);
        MG_CHECK(fs::current_path() == fixture.root);
    }
    fixture.close();
}
std::string bytes(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    MG_CHECK(stream.good());
    return {std::istreambuf_iterator<char>(stream), {}};
}
void export_ownership() {
    Directory fixture("demo-ownership-" + std::to_string(GetCurrentProcessId()));
    fs::create_directory(L"existing");
    { std::ofstream sentinel(L"existing/sentinel"); sentinel << "retained"; }
    bool refused = false;
    try { export_to(L"ordinary", L"existing", false); } catch (const std::exception &) { refused = true; }
    MG_CHECK(refused && bytes(L"existing/sentinel") == "retained");
    MG_CHECK(fs::create_directory(L"existing-empty"));
    refused = false;
    try { export_to(L"ordinary", L"existing-empty", false); } catch (const std::exception &) { refused = true; }
    MG_CHECK(refused && fs::is_directory(L"existing-empty") && fs::is_empty(L"existing-empty"));
    refused = false;
    try { export_to(L"ordinary", L"missing-parent/export", false); } catch (const std::exception &) { refused = true; }
    MG_CHECK(refused && !fs::exists(L"missing-parent"));
    refused = false;
    try { export_to(L"unknown", L"invalid", false); } catch (const std::exception &) { refused = true; }
    MG_CHECK(refused && !fs::exists(L"invalid"));
    for (bool packed : {false,true}) {
        std::string reason;
        try { export_to(L"ordinary", L"incomplete", packed, [] { fs::remove(L"Matrix/Map/ordinary.cmap"); }); }
        catch (const std::exception &error) { reason = error.what(); }
        MG_CHECK(reason.find("fs::is_regular_file(map_path(profile))") != std::string::npos);
        MG_CHECK(fs::current_path() == fixture.root && !fs::exists(L"incomplete"));
    }
    MG_CHECK(bytes(L"existing/sentinel") == "retained");
    fixture.close();
}
std::wstring quote_argument(std::wstring_view value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(character == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        result += character; slashes = 0;
    }
    result.append(2 * slashes, L'\\');
    return result + L'"';
}
DWORD run_cli(std::initializer_list<std::wstring> arguments) {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    MG_CHECK(length && length < executable.size()); executable.resize(length);
    std::wstring command = quote_argument(executable);
    for (const auto &argument : arguments) command += L" " + quote_argument(argument);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    MG_CHECK(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process));
    struct Process { PROCESS_INFORMATION value; ~Process() { CloseHandle(value.hThread); CloseHandle(value.hProcess); } } owned{process};
    const auto waited = WaitForSingleObject(process.hProcess, 15000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 99);
        MG_CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
        throw std::runtime_error("Demo export child did not finish within 15 seconds");
    }
    DWORD exit = 0; MG_CHECK(GetExitCodeProcess(process.hProcess, &exit)); return exit;
}
void export_cli() {
    Directory fixture("demo-cli-" + std::to_string(GetCurrentProcessId()));
    for (const auto *mode : {L"Text",L"Packed"}) {
        const auto destination = fixture.root / (std::wstring(mode) + L"-\u0416 \u03bb");
        MG_CHECK(run_cli({L"--export",L"fights",destination.wstring(),mode}) == 0);
        const bool packed = std::wstring_view(mode) == L"Packed";
        MG_CHECK(fs::is_regular_file(destination / L"CFG/robots.dat") == packed);
        MG_CHECK(fs::is_directory(destination / L"CFG/robots") == !packed);
        MG_CHECK(fs::is_directory(destination / L"parked-text-config") == packed);
        const auto map = destination / map_path(L"fights");
        const auto original = bytes(map);
        MG_CHECK(run_cli({L"--export",L"ordinary",destination.wstring(),mode}) == 1);
        MG_CHECK(bytes(map) == original && !fs::exists(destination / map_path(L"ordinary")));
    }
    MG_CHECK(run_cli({}) == 2);
    MG_CHECK(run_cli({L"--help"}) == 0);
    MG_CHECK(run_cli({L"--export"}) == 1);
    MG_CHECK(run_cli({L"--export",L"unknown",L"invalid-profile",L"Text"}) == 1);
    MG_CHECK(run_cli({L"--export",L"ordinary",L"invalid-mode",L"Other"}) == 1);
    MG_CHECK(run_cli({L"--export",L"ordinary",L"missing-parent/export",L"Text"}) == 1);
    MG_CHECK(!fs::exists(L"invalid-profile") && !fs::exists(L"invalid-mode") && !fs::exists(L"missing-parent"));
    fixture.close();
}
void calcvis_cli() {
    Directory fixture("calcvis-cli-"+std::to_string(GetCurrentProcessId()));
    for(const auto *profile : {L"calcvis-flat",L"calcvis-ridge"}) for(const auto *mode : {L"Text",L"Packed"}) {
        const auto destination=fixture.root/(std::wstring(profile)+L"-"+mode);
        MG_CHECK(run_cli({L"--export",profile,destination.wstring(),mode})==0);
        const auto original=bytes(destination/map_path(profile));
        MG_CHECK(run_cli({L"--verify-calcvis",profile,destination.wstring(),mode})==0);
        MG_CHECK(bytes(destination/map_path(profile))==original);
        MG_CHECK(run_cli({L"--verify-calcvis",profile,destination.wstring(),std::wstring_view(mode)==L"Text"?L"Packed":L"Text"})==1);
        // The verifier rejects a different, otherwise valid all-land map without the controlled camera properties.
        tests::write_native_session_map(destination/map_path(profile));
        MG_CHECK(run_cli({L"--verify-calcvis",profile,destination.wstring(),mode})==1);
    }
    MG_CHECK(run_cli({L"--verify-calcvis",L"ordinary",L"missing",L"Text"})==1);
    MG_CHECK(!fs::exists(L"missing"));
    fixture.close();
}
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit(); SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result;
    if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) {
        std::cout << "Usage: " << argv[0] << " --export <profile> <new-directory> <Text|Packed>\n"
                     "Profiles: ordinary, reinforcement, no-reinforcement, win, loss, fights, calcvis-flat, calcvis-ridge\n"
                     "Use --verify-calcvis <calcvis-profile> <existing-directory> <Text|Packed> before native execution.\n"
                     "Without --export, use a named test or --list. No game is launched.\n";
        result = argc == 1 ? 2 : 0;
    } else if (argc >= 2 && std::strcmp(argv[1], "--verify-calcvis") == 0) {
        const auto previous=fs::current_path();
        try {
            int count=0;auto **wide=CommandLineToArgvW(GetCommandLineW(),&count);MG_CHECK(wide);
            struct Arguments { wchar_t **value; ~Arguments(){LocalFree(value);} } arguments{wide};
            MG_CHECK(count==5 && calcvis(wide[2]) && (std::wstring_view(wide[4])==L"Text" || std::wstring_view(wide[4])==L"Packed"));
            fs::current_path(fs::absolute(wide[3]));
            validate(wide[2],std::wstring_view(wide[4])==L"Packed",true);
            std::cout<<"Controlled CalcVis export verified; no game launched.\n";result=0;
        } catch(const CException &error){std::wcerr<<error.Info()<<'\n';result=1;}
        catch(const std::exception &error){std::cerr<<error.what()<<'\n';result=1;}
        fs::current_path(previous);
    } else if (argc >= 2 && std::strcmp(argv[1], "--export") == 0) {
        try {
            int count = 0;
            auto **wide = CommandLineToArgvW(GetCommandLineW(), &count);
            MG_CHECK(wide);
            struct Arguments { wchar_t **value; ~Arguments() { LocalFree(value); } } arguments{wide};
            MG_CHECK(count == 5 && (std::wstring_view(wide[4]) == L"Text" || std::wstring_view(wide[4]) == L"Packed"));
            export_to(wide[2], wide[3], std::wstring_view(wide[4]) == L"Packed");
            std::cout << "Synthetic demo export complete; no game launched.\n"; result = 0;
        } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    } else {
        const tests::Case cases[] = {
            {"game.session.demo_export_layout_ordinary", [] { export_layout(L"ordinary"); }},
            {"game.session.demo_export_layout_reinforcement", [] { export_layout(L"reinforcement"); }},
            {"game.session.demo_export_layout_no_reinforcement", [] { export_layout(L"no-reinforcement"); }},
            {"game.session.demo_export_layout_win", [] { export_layout(L"win"); }},
            {"game.session.demo_export_layout_loss", [] { export_layout(L"loss"); }},
            {"game.session.demo_export_layout_fights", [] { export_layout(L"fights"); }},
            {"game.presentation.calcvis_export_cli",calcvis_cli},
            {"game.presentation.calcvis_export_flat", [] { export_layout(L"calcvis-flat"); }},
            {"game.presentation.calcvis_export_ridge", [] { export_layout(L"calcvis-ridge"); }},
            {"game.session.demo_export_ownership",export_ownership}, {"game.session.demo_export_cli",export_cli}};
        result = tests::run(argc, argv, cases);
    }
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit(); return result;
}
