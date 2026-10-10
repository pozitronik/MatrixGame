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

constexpr tests::Case cases[] = {
    {"game.startup.resources_missing", no_packages},
    {"game.startup.resources_optional", optional_layouts},
    {"game.startup.resources_exact_files", exact_files_only},
    {"game.startup.resources_unopened", unopened_archive},
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
