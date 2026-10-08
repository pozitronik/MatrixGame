// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "CStorage.hpp"

#include <array>
#include <exception>
#include <format>
#include <type_traits>
#include "stupid_logger.hpp"

logger_type lgr{std::cerr};

namespace {

static_assert(std::is_nothrow_move_constructible_v<Base::CStorageRecordItem>);
static_assert(std::is_nothrow_move_constructible_v<Base::CStorageRecord>);
static_assert(std::is_nothrow_move_assignable_v<Base::CStorageRecord>);

void check_tree(const Base::CBlockPar &expected, const Base::CBlockPar &actual) {
    MG_CHECK(actual.ParCount() == expected.ParCount());
    MG_CHECK(actual.BlockCount() == expected.BlockCount());
    for (int i = 0; i < expected.ParCount(); ++i) {
        MG_CHECK(actual.ParGetName(i) == expected.ParGetName(i));
        MG_CHECK(actual.ParGet(i) == expected.ParGet(i));
    }
    for (int i = 0; i < expected.BlockCount(); ++i) {
        MG_CHECK(actual.BlockGetName(i) == expected.BlockGetName(i));
        check_tree(*expected.BlockGet(i), *actual.BlockGet(i));
    }
}

void populate_tree(Base::CBlockPar &tree) {
    tree.ParAdd(L"duplicate", L"first");
    tree.ParAdd(L"duplicate", L"second");
    tree.ParAdd(L"empty", L"");
    tree.ParAdd(L"unicode", L"\u041f\u0440\u0438\u0432\u0435\u0442");
    tree.ParAdd(L"embedded-null", std::wstring(L"a\0b", 3));
    tree.ParAdd(L"large", std::wstring(40000, L'x'));
    for (int i = 0; i < 64; ++i) {
        auto *child = tree.BlockAdd(L"child-" + std::to_wstring(i % 8));
        child->ParAdd(L"value", std::to_wstring(i));
        child->BlockAdd(L"nested")->ParAdd(L"value", L"leaf");
    }
}

void scalar_schema(Base::CStorageRecord &schema) {
    schema.AddItem(Base::CStorageRecordItem(L"v", Base::ST_INT32));
}

void nested_growth() {
    Base::CBlockPar source;
    source.ParAdd(L"root-value", L"present");
    for (int i = 0; i < 64; ++i) {
        auto *child = source.BlockAdd(L"child-" + std::to_wstring(i));
        child->ParAdd(L"value", std::to_wstring(i));
    }
    Base::CStorage storage;
    storage.StoreBlockPar(L"root", source);
    Base::CBlockPar restored;
    storage.RestoreBlockPar(L"root", restored);
    MG_CHECK(restored.ParGet(L"root-value") == L"present");
    MG_CHECK(restored.BlockCount() == 64);
    for (int i = 0; i < 64; ++i) {
        const auto *child = restored.BlockGet(i);
        MG_CHECK(child->ParGet(L"value") == std::to_wstring(i));
    }
}

void legacy_format() {
    Base::CStorageRecord schema(L"t");
    schema.AddItem(Base::CStorageRecordItem(L"v", Base::ST_INT32));
    Base::CStorage storage;
    storage.AddRecord(schema);
    auto *values = storage.GetBuf(L"t", L"v", Base::ST_INT32);
    MG_CHECK(values != nullptr);
    values->AddArray();
    values->AddToArray<int32_t>(0, 27);
    values->AddToArray<int32_t>(0, 39);
    Base::CBuf bytes;
    storage.Save(bytes);
    constexpr std::array<byte, 64> expected = {
        0x53, 0x54, 0x52, 0x47, 0, 0, 0, 0, 1, 0, 0, 0,
        0x74, 0, 0, 0, 1, 0, 0, 0, 0x76, 0, 0, 0,
        0, 0, 0, 0, 32, 0, 0, 0, 20, 0, 0, 0,
        1, 0, 0, 0, 4, 0, 0, 0, 27, 0, 0, 0,
        39, 0, 0, 0, 12, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0,
    };
    MG_CHECK(bytes.Len() == expected.size());
    MG_CHECK(std::memcmp(bytes.Get(), expected.data(), expected.size()) == 0);
}

void round_trip(bool compression) {
    Base::CBlockPar source;
    populate_tree(source);
    Base::CStorage storage;
    storage.StoreBlockPar(L"root", source);
    Base::CBuf encoded;
    storage.Save(encoded, compression);
    const auto identity = storage.CalcUniqID();
    Base::CStorage loaded;
    MG_CHECK(loaded.Load(encoded));
    MG_CHECK(loaded.CalcUniqID() == identity);
    Base::CBlockPar restored;
    loaded.RestoreBlockPar(L"root", restored);
    check_tree(source, restored);
}

void uncompressed_round_trip() { round_trip(false); }
void compressed_round_trip() { round_trip(true); }

void empty_round_trip() {
    for (bool compression : {false, true}) {
        Base::CStorage source;
        Base::CBuf encoded;
        source.Save(encoded, compression);
        Base::CStorage loaded;
        MG_CHECK(loaded.Load(encoded));
        MG_CHECK(!loaded.IsTablePresent(L"t"));
        loaded.Clear();
    }
}

void records_and_columns() {
    Base::CStorage storage;
    for (int i = 0; i < 64; ++i) {
        Base::CStorageRecord schema(L"record-" + std::to_wstring(i));
        for (int column = 0; column < 64; ++column) {
            schema.AddItem(Base::CStorageRecordItem(L"column-" + std::to_wstring(column), Base::ST_INT32));
        }
        storage.AddRecord(schema);
        for (int column = 0; column < 64; ++column) {
            auto *values = storage.GetBuf(schema.GetName().c_str(),
                                         (L"column-" + std::to_wstring(column)).c_str(), Base::ST_INT32);
            MG_CHECK(values != nullptr);
            values->AddArray();
            values->AddToArray<int32_t>(0, i * 1000 + column);
        }
    }
    for (int i = 0; i < 64; ++i) {
        for (int column = 0; column < 64; ++column) {
            auto *values = storage.GetBuf((L"record-" + std::to_wstring(i)).c_str(),
                                         (L"column-" + std::to_wstring(column)).c_str(), Base::ST_INT32);
            MG_CHECK(values != nullptr);
            MG_CHECK(values->GetFirst<int32_t>(0)[0] == i * 1000 + column);
        }
    }
}

void deletion_and_reuse() {
    Base::CStorage storage;
    for (const auto *name : {L"a", L"b", L"c", L"d"}) {
        Base::CStorageRecord schema(name);
        scalar_schema(schema);
        storage.AddRecord(schema);
        auto *values = storage.GetBuf(name, L"v", Base::ST_INT32);
        values->AddArray();
        values->AddToArray<int32_t>(0, name[0]);
    }
    auto *survivor = storage.GetBuf(L"d", L"v", Base::ST_INT32);
    storage.DelRecord(L"b");
    MG_CHECK(!storage.IsTablePresent(L"b"));
    MG_CHECK(storage.GetBuf(L"d", L"v", Base::ST_INT32) == survivor);
    MG_CHECK(survivor->GetFirst<int32_t>(0)[0] == L'd');
    Base::CBuf encoded;
    storage.Save(encoded);
    encoded.Pointer(8);
    MG_CHECK(encoded.Get<DWORD>() == 3);
    for (const auto *name : {L"a", L"d", L"c"}) {
        Base::CStorageRecord record(nullptr);
        MG_CHECK(record.Load(encoded));
        MG_CHECK(record.GetName() == name);
    }
    storage.DelRecord(L"missing");
    storage.DelRecord(L"c");
    storage.DelRecord(L"a");
    storage.DelRecord(L"d");
    MG_CHECK(!storage.IsTablePresent(L"d"));
    Base::CStorageRecord replacement(L"replacement");
    scalar_schema(replacement);
    storage.AddRecord(replacement);
    MG_CHECK(storage.GetBuf(L"replacement", L"v", Base::ST_INT32) != nullptr);
    storage.Clear();
    storage.Clear();
}

void schema_copy() {
    Base::CStorageRecord schema(L"t");
    scalar_schema(schema);
    Base::CStorage storage;
    storage.AddRecord(schema);
    auto *values = storage.GetBuf(L"t", L"v", Base::ST_INT32);
    values->AddArray();
    values->AddToArray<int32_t>(0, 42);
    Base::CBuf encoded;
    storage.Save(encoded);
    encoded.Pointer(12);
    Base::CStorageRecord original(nullptr);
    MG_CHECK(original.Load(encoded));
    Base::CStorageRecord copy(original);
    auto *copied_values = copy.GetBuf(L"v", Base::ST_INT32);
    MG_CHECK(copy.GetName() == original.GetName());
    MG_CHECK(copied_values != nullptr);
    MG_CHECK(copied_values->GetArraysCount() == 0);
    MG_CHECK(copied_values != original.GetBuf(L"v", Base::ST_INT32));
    copied_values->AddArray();
    copied_values->AddToArray<int32_t>(0, 99);
    MG_CHECK(original.GetBuf(L"v", Base::ST_INT32)->GetFirst<int32_t>(0)[0] == 42);
    Base::CStorageRecord moved(std::move(copy));
    MG_CHECK(moved.GetBuf(L"v", Base::ST_INT32) == copied_values);
    MG_CHECK(moved.GetBuf(L"v", Base::ST_INT32)->GetFirst<int32_t>(0)[0] == 99);
}

void column_compression() {
    Base::CStorageRecord schema(L"t");
    scalar_schema(schema);
    schema.AddItem(Base::CStorageRecordItem(L"text", Base::ST_WCHAR));
    Base::CStorage storage;
    storage.AddRecord(schema);
    auto *values = storage.GetBuf(L"t", L"v", Base::ST_INT32);
    values->AddArray();
    values->AddToArray<int32_t>(0, 42);
    storage.GetBuf(L"t", L"text", Base::ST_WCHAR)->AddWStr(L"payload");
    Base::CBuf plain;
    storage.Save(plain);
    plain.Pointer(12);
    Base::CStorageRecord record(nullptr);
    MG_CHECK(record.Load(plain));
    Base::CBuf encoded;
    record.Save(encoded, true);
    encoded.Pointer(0);
    Base::CStorageRecord loaded(nullptr);
    MG_CHECK(loaded.Load(encoded));
    MG_CHECK(encoded.Pointer() == encoded.Len());
    auto *integers = loaded.GetBuf(L"v", Base::ST_INT32);
    auto *text = loaded.GetBuf(L"text", Base::ST_WCHAR);
    MG_CHECK(integers != nullptr && text != nullptr);
    MG_CHECK(integers->GetFirst<int32_t>(0)[0] == 42);
    MG_CHECK(text->GetAsWStr(0) == L"payload");
}

void partial_load_cleanup() {
    Base::CBuf truncated;
    truncated.Add<DWORD>(0x47525453);
    truncated.Add<DWORD>(0);
    truncated.Add<DWORD>(1);
    truncated.WStr(L"t");
    truncated.Add<DWORD>(1);
    truncated.WStr(L"v");
    truncated.Add<DWORD>(Base::ST_INT32);
    truncated.Add<DWORD>(128);
    Base::CStorage storage;
    bool rejected = false;
    try {
        storage.Load(truncated);
    }
    catch (const Base::CException &) {
        rejected = true;
    }
    MG_CHECK(rejected);
    storage.Clear();
    Base::CStorageRecord schema(L"reused");
    scalar_schema(schema);
    storage.AddRecord(schema);
    MG_CHECK(storage.GetBuf(L"reused", L"v", Base::ST_INT32) != nullptr);
}

void check_rejected_header(DWORD tag, DWORD version) {
    Base::CBuf rejected;
    rejected.Add<DWORD>(tag);
    rejected.Add<DWORD>(version);
    Base::CStorage storage;
    MG_CHECK(!storage.Load(rejected));

    Base::CBlockPar source;
    source.ParAdd(L"setting", L"preserved");
    Base::CStorage valid;
    valid.StoreBlockPar(L"da", source);
    Base::CBuf encoded;
    valid.Save(encoded);
    MG_CHECK(storage.Load(encoded));
    Base::CBlockPar restored;
    storage.RestoreBlockPar(L"da", restored);
    check_tree(source, restored);
}

void invalid_tag() {
    check_rejected_header(0, 0);
}

void unsupported_version() {
    check_rejected_header(0x47525453, 2);
}

void buildcfg_smoke() {
    Base::CBlockPar interface_config;
    interface_config.LoadFromTextFile(L"MatrixGame/CFG/robots/iface.txt");
    Base::CBlockPar input;
    input.LoadFromTextFile(L"MatrixGame/CFG/robots/data.txt");
    Base::CBlockPar data;
    data.CopyFrom(input);
    if (data.BlockGetNE(L"Replaces")) {
        data.BlockDelete(L"Replaces");
    }
    Base::CStorage storage;
    storage.StoreBlockPar(L"if", interface_config);
    storage.StoreBlockPar(L"da", data);
    Base::CBuf encoded;
    storage.Save(encoded, true);
    Base::CStorage loaded;
    MG_CHECK(loaded.Load(encoded));
    Base::CBlockPar restored_interface;
    Base::CBlockPar restored_data;
    loaded.RestoreBlockPar(L"if", restored_interface);
    loaded.RestoreBlockPar(L"da", restored_data);
    check_tree(interface_config, restored_interface);
    check_tree(data, restored_data);
}

constexpr tests::Case cases[] = {
    {"base.storage.nested_growth", nested_growth},
    {"base.storage.legacy_format", legacy_format},
    {"base.storage.records_and_columns", records_and_columns},
    {"base.storage.deletion_and_reuse", deletion_and_reuse},
    {"base.storage.schema_copy", schema_copy},
    {"base.storage.uncompressed_round_trip", uncompressed_round_trip},
    {"base.storage.compressed_round_trip", compressed_round_trip},
    {"base.storage.empty_round_trip", empty_round_trip},
    {"base.storage.column_compression", column_compression},
    {"base.storage.partial_load_cleanup", partial_load_cleanup},
    {"base.storage.invalid_tag", invalid_tag},
    {"base.storage.unsupported_version", unsupported_version},
};

}  // namespace

int main(int argc, char **argv) {
    // A native crash must terminate the case instead of opening an error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    DTRACE();
    // The local smoke check uses tracked configuration, leaving supplied robots.dat untouched.
    constexpr tests::Case local_cases[] = {{"manual.storage.buildcfg", buildcfg_smoke}};
    const bool local = argc == 2 && std::string_view(argv[1]) == "manual.storage.buildcfg";
    const int result = tests::run(argc, argv, local ? std::span<const tests::Case>(local_cases)
                                                  : std::span<const tests::Case>(cases));
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Storage test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
