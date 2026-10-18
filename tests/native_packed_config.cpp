// MatrixGame - licensed under GPLv2 or any later version.
#include "native_packed_config.hpp"
#include "CBlockPar.hpp"
#include "CStorage.hpp"
#include "test_support.hpp"
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
}
