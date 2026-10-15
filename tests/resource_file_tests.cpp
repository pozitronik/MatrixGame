// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "ResourceFiles.hpp"
#include "CFile.hpp"
#include "stupid_logger.hpp"

#include <fstream>

logger_type lgr{std::cerr};

namespace {

struct Files {
    const std::filesystem::path root = std::filesystem::current_path() /
        ("matrixgame-resource-files-" + std::to_string(GetCurrentProcessId()));
    Files() {
        MG_CHECK(std::filesystem::create_directory(root));
        MG_CHECK(std::filesystem::create_directory(root / "DATA"));
    }
    ~Files() {
        Base::CFile::ReleasePackFiles();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    auto path(const wchar_t *name) const { return root / "DATA" / name; }
    void add(const wchar_t *name) {
        std::ofstream file(path(name), std::ios::binary);
        file.put('\0');
        MG_CHECK(file.good());
    }
    // A package with an empty root folder, which opens successfully.
    void add_package(const wchar_t *name) {
        std::ofstream file(path(name), std::ios::binary);
        for (const uint32_t value : {4u, 12u, 0u, 158u})
            for (int shift = 0; shift < 32; shift += 8) file.put(char((value >> shift) & 0xff));
        MG_CHECK(file.good());
    }
};

void no_packages() {
    Files files;
    for (const auto *name : {L"robots.pkg", L"sound.pkg", L"voices.pkg"})
        MG_CHECK(!Startup::ResourceFileExists(files.path(name)));
}

void optional_layouts() {
    Files files;
    files.add(L"robots.pkg");
    MG_CHECK(Startup::ResourceFileExists(files.path(L"robots.pkg")));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"sound.pkg")));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"voices.pkg")));
    files.add(L"sound.pkg");
    MG_CHECK(Startup::ResourceFileExists(files.path(L"sound.pkg")));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"voices.pkg")));
    files.add(L"voices.pkg");
    MG_CHECK(Startup::ResourceFileExists(files.path(L"voices.pkg")));
    std::filesystem::remove(files.path(L"sound.pkg"));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"sound.pkg")));
    MG_CHECK(Startup::ResourceFileExists(files.path(L"voices.pkg")));
}

void exact_files_only() {
    Files files;
    files.add(L"robots.pkg.backup");
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"robots.pkg")));
    MG_CHECK(std::filesystem::create_directory(files.path(L"sound.pkg")));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"sound.pkg")));
}

void unopened_archive() {
    Files files;
    Base::CFile::AddPackFile(files.path(L"registered-but-unopened.pkg").c_str());
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"sound.pkg")));
    MG_CHECK(!Startup::ResourceFileExists(files.path(L"voices.pkg")));
    files.add(L"sound.pkg");
    MG_CHECK(Startup::ResourceFileExists(files.path(L"sound.pkg")));
}

void damaged_packages() {
    {
        // A damaged optional package counts as missing; the packages that opened stay available.
        Files files;
        files.add_package(L"robots.pkg");
        files.add(L"sound.pkg");
        files.add_package(L"voices.pkg");
        files.add_package(L"music.pkg");
        const auto packages = Startup::OpenPackages(files.root / "DATA", true);
        MG_CHECK(!packages.sound && packages.voices && packages.music && packages.any());
    }
    {
        // music.pkg is left alone without a music decoder.
        Files files;
        files.add_package(L"robots.pkg");
        files.add_package(L"music.pkg");
        const auto packages = Startup::OpenPackages(files.root / "DATA", false);
        MG_CHECK(!packages.sound && !packages.voices && !packages.music && !packages.any());
        MG_CHECK(Startup::ResourceFileExists(files.path(L"music.pkg")));
    }
    {
        // Only damaged or missing packages leave audio disabled.
        Files files;
        files.add(L"robots.pkg");
        files.add(L"sound.pkg");
        files.add(L"voices.pkg");
        const auto packages = Startup::OpenPackages(files.root / "DATA", true);
        MG_CHECK(!packages.any());
    }
    {
        // A valid sound package still opens when robots.pkg is absent.
        Files files;
        files.add_package(L"sound.pkg");
        const auto packages = Startup::OpenPackages(files.root / "DATA", true);
        MG_CHECK(packages.sound && !packages.voices && !packages.music);
    }
}

constexpr tests::Case cases[] = {
    {"game.startup.resources_missing", no_packages},
    {"game.startup.resources_optional", optional_layouts},
    {"game.startup.resources_exact_files", exact_files_only},
    {"game.startup.resources_unopened", unopened_archive},
    {"game.startup.resources_damaged", damaged_packages},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block) {
        std::cerr << "Resource file test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
