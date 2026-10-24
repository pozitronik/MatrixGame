// MatrixGame - licensed under GPLv2 or any later version.
#include "native_packed_config.hpp"
#include "CBlockPar.hpp"
#include "CStorage.hpp"
#include "test_support.hpp"
#include "StringConstants.hpp"
#include <filesystem>

namespace tests {
void pack_native_session_configuration() {
    namespace fs = std::filesystem;
    MG_CHECK(fs::is_regular_file(L"CFG/robots/data.txt"));
    MG_CHECK(fs::is_regular_file(L"CFG/robots/iface.txt"));
    MG_CHECK(!fs::exists(L"CFG/robots.dat") && !fs::exists(L"parked-text-config"));
    Base::CBlockPar data, interface_config;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    interface_config.LoadFromTextFile(L"CFG/robots/iface.txt");
    Base::CStorage storage;
    storage.StoreBlockPar(L"da", data);
    storage.StoreBlockPar(L"if", interface_config);
    storage.Save(L"CFG/robots.dat", true);
    // A successful initialization must read both packed trees, not loose files.
    fs::rename(L"CFG/robots", L"parked-text-config");
    MG_CHECK(!fs::exists(L"CFG/robots/data.txt") && !fs::exists(L"CFG/robots/iface.txt"));
}
void write_native_packed_missing_hint() {
    Base::CStorage source;
    MG_CHECK(source.Load(L"CFG/robots.dat"));
    Base::CBlockPar data, interface_config;
    source.RestoreBlockPar(L"da", data);
    source.RestoreBlockPar(L"if", interface_config);
    auto *bitmaps = data.BlockGet(PAR_SOURCE_HINTS)->BlockGet(PAR_SOURCE_HINTS_BITMAPS);
    MG_CHECK(bitmaps->ParCount() > 0);
    const auto first = bitmaps->ParGetName(0);
    bitmaps->ParSetAdd(first, L"native-missing-hint.png");
    MG_CHECK(!std::filesystem::exists(L"native-missing-hint.png"));
    Base::CStorage changed;
    changed.StoreBlockPar(L"da", data);
    changed.StoreBlockPar(L"if", interface_config);
    changed.Save(L"CFG/robots.dat", true);
}

void write_native_packed_missing_hint_block(const std::filesystem::path &filename) {
    Base::CStorage source;
    MG_CHECK(source.Load(filename.c_str()));
    Base::CBlockPar data, interface_config;
    source.RestoreBlockPar(L"da", data);
    source.RestoreBlockPar(L"if", interface_config);
    data.BlockGet(PAR_SOURCE_HINTS)->BlockDelete(PAR_SOURCE_HINTS_BITMAPS);
    Base::CStorage changed;
    changed.StoreBlockPar(L"da", data);
    changed.StoreBlockPar(L"if", interface_config);
    changed.Save(filename.c_str(), true);
}

}
