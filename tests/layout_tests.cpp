// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "wire_bytes.hpp"

#include "CFile.hpp"
#include "CStorage.hpp"
#include "Pack.hpp"
#include "Common.hpp"
#include "VectorObject.hpp"
#include "stupid_logger.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

logger_type lgr{std::cerr};

// Wire layouts are specified in docs/DATA_FORMATS.md. Fixtures below are explicit byte sequences built with
// fixed field widths; production readers must parse them and production writers must emit the same bytes.

namespace {

using wire::Bytes;
using Arrays = std::vector<Bytes>;

constexpr uint32_t storage_tag = 0x47525453;  // "STRG"
constexpr uint32_t compressed_column = 0x80000000;

enum class Load { accepted, rejected, exception };

Base::CBuf to_buf(std::span<const uint8_t> bytes) {
    Base::CBuf buf;
    buf.Add(bytes.data(), bytes.size());
    return buf;
}

std::vector<uint8_t> from_buf(const Base::CBuf &buf) {
    const auto *begin = static_cast<const uint8_t *>(buf.Get());
    return {begin, begin + buf.Len()};
}

Load load(Base::CStorage &storage, std::span<const uint8_t> bytes) {
    Base::CBuf buf = to_buf(bytes);
    try {
        return storage.Load(buf) ? Load::accepted : Load::rejected;
    }
    catch (const Base::CException &) {
        return Load::exception;
    }
}

// Column body: 12-byte header, element arrays in index order, then one 12-byte entry per array.
Bytes column_body(uint32_t element_size, const Arrays &arrays) {
    uint32_t data = 0;
    for (const auto &array : arrays) data += array.size();
    Bytes body;
    body.u32(12 + data).u32(static_cast<uint32_t>(arrays.size())).u32(element_size);
    for (const auto &array : arrays) body.raw(array);
    uint32_t offset = 12;
    for (const auto &array : arrays) {
        const uint32_t count = array.size() / element_size;
        body.u32(offset).u32(count).u32(count);
        offset += array.size();
    }
    return body;
}

Bytes &column(Bytes &out, std::u16string_view name, uint32_t type, const Bytes &body) {
    return out.utf16z(name).u32(type).u32(body.size()).raw(body);
}

Bytes utf16_array(std::u16string_view value) { return Bytes().utf16(value); }

// --- STRG scalar columns ---------------------------------------------------------------------------------

Bytes scalar_fixture() {
    Bytes out;
    out.u32(storage_tag).u32(0).u32(2);
    out.utf16z(u"t").u32(6);
    column(out, u"i", Base::ST_INT32, column_body(4, {Bytes().u32(0xfffffffe).u32(0x7fffffff), Bytes()}));
    column(out, u"d", Base::ST_DWORD, column_body(4, {Bytes().u32(0xa1b2c3d4)}));
    column(out, u"b", Base::ST_BYTE, column_body(1, {Bytes().u8(0).u8(0xff).u8(7)}));
    column(out, u"f", Base::ST_FLOAT, column_body(4, {Bytes().u32(0x3fc00000)}));            // 1.5f
    column(out, u"g", Base::ST_DOUBLE, column_body(8, {Bytes().u64(0xc002000000000000ull)}));  // -2.25
    column(out, u"w", Base::ST_WCHAR, column_body(2, {utf16_array(u"Я!"), Bytes()}));
    out.utf16z(u"empty").u32(0);
    return out;
}

Base::CStorage scalar_storage() {
    Base::CStorageRecord schema(L"t");
    schema.AddItem(Base::CStorageRecordItem(L"i", Base::ST_INT32));
    schema.AddItem(Base::CStorageRecordItem(L"d", Base::ST_DWORD));
    schema.AddItem(Base::CStorageRecordItem(L"b", Base::ST_BYTE));
    schema.AddItem(Base::CStorageRecordItem(L"f", Base::ST_FLOAT));
    schema.AddItem(Base::CStorageRecordItem(L"g", Base::ST_DOUBLE));
    schema.AddItem(Base::CStorageRecordItem(L"w", Base::ST_WCHAR));
    Base::CStorage storage;
    storage.AddRecord(schema);
    storage.AddRecord(Base::CStorageRecord(L"empty"));
    auto *integers = storage.GetBuf(L"t", L"i", Base::ST_INT32);
    integers->AddArray();
    integers->AddToArray<int32_t>(0, -2);
    integers->AddToArray<int32_t>(0, 0x7fffffff);
    integers->AddArray();
    auto *words = storage.GetBuf(L"t", L"d", Base::ST_DWORD);
    words->AddArray();
    words->AddToArray<uint32_t>(0, 0xa1b2c3d4);
    auto *bytes = storage.GetBuf(L"t", L"b", Base::ST_BYTE);
    bytes->AddArray();
    for (uint8_t value : {0, 0xff, 7}) bytes->AddToArray<uint8_t>(0, value);
    auto *floats = storage.GetBuf(L"t", L"f", Base::ST_FLOAT);
    floats->AddArray();
    floats->AddToArray<float>(0, 1.5f);
    auto *doubles = storage.GetBuf(L"t", L"g", Base::ST_DOUBLE);
    doubles->AddArray();
    doubles->AddToArray<double>(0, -2.25);
    auto *text = storage.GetBuf(L"t", L"w", Base::ST_WCHAR);
    text->AddWStr(L"Я!");
    text->AddWStr(L"");
    return storage;
}

template <class T>
T element(Base::CDataBuf *buf, int array, int index) {
    T value;
    std::memcpy(&value, buf->GetFirst<uint8_t>(array) + index * sizeof(T), sizeof(T));
    return value;
}

void check_scalar_values(Base::CStorage &storage) {
    auto *integers = storage.GetBuf(L"t", L"i", Base::ST_INT32);
    MG_CHECK(integers && integers->GetArraysCount() == 2);
    MG_CHECK(integers->GetArrayLength(0) == 2 && integers->GetArrayLength(1) == 0);
    MG_CHECK(element<int32_t>(integers, 0, 0) == -2 && element<int32_t>(integers, 0, 1) == 0x7fffffff);
    auto *words = storage.GetBuf(L"t", L"d", Base::ST_DWORD);
    MG_CHECK(words && words->GetArraysCount() == 1 && element<uint32_t>(words, 0, 0) == 0xa1b2c3d4);
    auto *bytes = storage.GetBuf(L"t", L"b", Base::ST_BYTE);
    MG_CHECK(bytes && bytes->GetArrayLength(0) == 3);
    MG_CHECK(element<uint8_t>(bytes, 0, 0) == 0 && element<uint8_t>(bytes, 0, 1) == 0xff);
    MG_CHECK(element<uint8_t>(bytes, 0, 2) == 7);
    MG_CHECK(element<float>(storage.GetBuf(L"t", L"f", Base::ST_FLOAT), 0, 0) == 1.5f);
    MG_CHECK(element<double>(storage.GetBuf(L"t", L"g", Base::ST_DOUBLE), 0, 0) == -2.25);
    auto *text = storage.GetBuf(L"t", L"w", Base::ST_WCHAR);
    MG_CHECK(text && text->GetArraysCount() == 2);
    MG_CHECK(text->GetAsWStr(0) == L"Я!" && text->GetAsWStr(1).empty());
    // Column lookup requires the stored element type.
    MG_CHECK(storage.GetBuf(L"t", L"i", Base::ST_DWORD) == nullptr);
    MG_CHECK(storage.IsTablePresent(L"empty"));
}

void storage_columns() {
    const Bytes expected = scalar_fixture();
    auto storage = scalar_storage();
    Base::CBuf written;
    storage.Save(written);
    MG_CHECK(from_buf(written) == expected.data);

    Base::CStorage loaded;
    MG_CHECK(load(loaded, expected.data) == Load::accepted);
    check_scalar_values(loaded);
    Base::CBuf rewritten;
    loaded.Save(rewritten);
    MG_CHECK(from_buf(rewritten) == expected.data);
}

// --- CBlockPar tree representation ----------------------------------------------------------------------

struct TreeRecord {
    std::u16string name;
    std::vector<std::u16string> parameter_names, parameter_values, block_names, block_records;
};

Bytes wchar_column(std::u16string_view name, const std::vector<std::u16string> &values) {
    Arrays arrays;
    for (const auto &value : values) arrays.push_back(utf16_array(value));
    Bytes out;
    return column(out, name, Base::ST_WCHAR, column_body(2, arrays));
}

Bytes tree_fixture(const std::vector<TreeRecord> &records) {
    Bytes out;
    out.u32(storage_tag).u32(0).u32(static_cast<uint32_t>(records.size()));
    for (const auto &record : records) {
        out.utf16z(record.name).u32(4);
        out.raw(wchar_column(u"0", record.parameter_names));
        out.raw(wchar_column(u"1", record.parameter_values));
        out.raw(wchar_column(u"2", record.block_names));
        out.raw(wchar_column(u"3", record.block_records));
    }
    return out;
}

// Records are stored parent first; child records receive the smallest unused decimal name.
const std::vector<TreeRecord> tree_records = {
    {u"da", {u"Name", u"Name"}, {u"Value", u"Second"}, {u"A", u"B"}, {u"0", u"2"}},
    {u"0", {u"k"}, {u"1"}, {u"Deep"}, {u"1"}},
    {u"1", {u"x"}, {u""}, {}, {}},
    {u"2", {}, {}, {}, {}},
};

void fill_tree(Base::CBlockPar &tree) {
    tree.ParAdd(L"Name", L"Value");
    tree.ParAdd(L"Name", L"Second");
    auto *a = tree.BlockAdd(L"A");
    a->ParAdd(L"k", L"1");
    a->BlockAdd(L"Deep")->ParAdd(L"x", L"");
    tree.BlockAdd(L"B");
}

void check_tree(const Base::CBlockPar &expected, const Base::CBlockPar &actual) {
    MG_CHECK(actual.ParCount() == expected.ParCount() && actual.BlockCount() == expected.BlockCount());
    for (int i = 0; i < expected.ParCount(); ++i) {
        MG_CHECK(actual.ParGetName(i) == expected.ParGetName(i) && actual.ParGet(i) == expected.ParGet(i));
    }
    for (int i = 0; i < expected.BlockCount(); ++i) {
        MG_CHECK(actual.BlockGetName(i) == expected.BlockGetName(i));
        check_tree(*expected.BlockGet(i), *actual.BlockGet(i));
    }
}

void storage_tree() {
    const Bytes expected = tree_fixture(tree_records);
    Base::CBlockPar source;
    fill_tree(source);
    Base::CStorage storage;
    storage.StoreBlockPar(L"da", source);
    Base::CBuf written;
    storage.Save(written);
    MG_CHECK(from_buf(written) == expected.data);

    Base::CStorage loaded;
    MG_CHECK(load(loaded, expected.data) == Load::accepted);
    Base::CBlockPar restored;
    loaded.RestoreBlockPar(L"da", restored);
    check_tree(source, restored);
}

// --- ZL03 framing ---------------------------------------------------------------------------------------

// A storage holding one ST_BYTE array; its version-zero body after the header is 48 + size bytes.
Bytes byte_storage_fixture(uint32_t size) {
    Bytes values;
    for (uint32_t i = 0; i < size; ++i) values.u8(static_cast<uint8_t>(i % 251));
    Bytes out;
    out.u32(storage_tag).u32(0).u32(1).utf16z(u"t").u32(1);
    return column(out, u"b", Base::ST_BYTE, column_body(1, {values}));
}

Bytes zl03(const std::vector<std::span<const uint8_t>> &blocks) {
    Bytes out;
    out.text("ZL03").u32(static_cast<uint32_t>(blocks.size()));
    for (auto block : blocks) {
        const Bytes stream = wire::zlib_stored(block);
        out.u32(stream.size()).raw(stream);
    }
    return out;
}

// Version one keeps the tag and version, then frames the version-zero bytes from offset 8 as ZL03.
Bytes compressed_storage(const Bytes &plain, size_t block_size) {
    std::span<const uint8_t> body(plain.data.begin() + 8, plain.data.end());
    std::vector<std::span<const uint8_t>> blocks;
    for (size_t offset = 0; offset < body.size(); offset += block_size) {
        blocks.push_back(body.subspan(offset, std::min(block_size, body.size() - offset)));
    }
    return Bytes().u32(storage_tag).u32(1).raw(zl03(blocks));
}

std::vector<uint8_t> inflate_block(std::span<const uint8_t> stream) {
    std::vector<uint8_t> output(65000);
    uLongf length = static_cast<uLongf>(output.size());
    MG_CHECK(uncompress(output.data(), &length, stream.data(), static_cast<uLong>(stream.size())) == Z_OK);
    output.resize(length);
    return output;
}

// Walks a ZL03 stream and returns the decoded block sizes and concatenated content.
std::pair<std::vector<size_t>, std::vector<uint8_t>> decode_zl03(std::span<const uint8_t> stream) {
    MG_CHECK(stream.size() >= 8 && std::memcmp(stream.data(), "ZL03", 4) == 0);
    const uint32_t count = wire::read_u32(stream, 4);
    size_t offset = 8;
    std::vector<size_t> sizes;
    std::vector<uint8_t> decoded;
    for (uint32_t i = 0; i < count; ++i) {
        MG_CHECK(offset + 4 <= stream.size());
        const uint32_t length = wire::read_u32(stream, offset);
        MG_CHECK(offset + 4 + length <= stream.size());
        const auto block = inflate_block(stream.subspan(offset + 4, length));
        sizes.push_back(block.size());
        decoded.insert(decoded.end(), block.begin(), block.end());
        offset += 4 + length;
    }
    MG_CHECK(offset == stream.size());
    return {sizes, decoded};
}

void storage_zl03() {
    // 140,000 data bytes produce a 140,048-byte body: two full 65,000-byte blocks and a 10,048-byte tail.
    const Bytes plain = byte_storage_fixture(140000);
    Base::CStorage storage;
    MG_CHECK(load(storage, plain.data) == Load::accepted);
    Base::CBuf uncompressed;
    storage.Save(uncompressed);
    MG_CHECK(from_buf(uncompressed) == plain.data);

    Base::CBuf written;
    storage.Save(written, true);
    const auto bytes = from_buf(written);
    MG_CHECK(wire::read_u32(bytes, 0) == storage_tag && wire::read_u32(bytes, 4) == 1);
    const auto [sizes, decoded] = decode_zl03(std::span<const uint8_t>(bytes).subspan(8));
    MG_CHECK(sizes == std::vector<size_t>({65000, 65000, 10048}));
    MG_CHECK(decoded == std::vector<uint8_t>(plain.data.begin() + 8, plain.data.end()));

    // Compressor-independent stored blocks, including a short non-final block, decode to the same content.
    for (size_t block_size : {size_t(65000), size_t(4096)}) {
        Base::CStorage loaded;
        MG_CHECK(load(loaded, compressed_storage(plain, block_size).data) == Load::accepted);
        auto *values = loaded.GetBuf(L"t", L"b", Base::ST_BYTE);
        MG_CHECK(values && values->GetArrayLength(0) == 140000);
        for (uint32_t i = 0; i < 140000; i += 997) MG_CHECK(element<uint8_t>(values, 0, i) == i % 251);
        Base::CBuf rewritten;
        loaded.Save(rewritten);
        MG_CHECK(from_buf(rewritten) == plain.data);
    }
}

void storage_column_zl03() {
    // A compressed column sets bit 31 of its type and stores a ZL03 stream of its body.
    const Bytes text = column_body(2, {utf16_array(u"payload"), utf16_array(u"ж")});
    const Bytes stream = zl03({std::span<const uint8_t>(text.data)});
    Bytes fixture;
    fixture.u32(storage_tag).u32(0).u32(1).utf16z(u"t").u32(1);
    column(fixture, u"w", Base::ST_WCHAR | compressed_column, stream);
    Base::CStorage storage;
    MG_CHECK(load(storage, fixture.data) == Load::accepted);
    auto *values = storage.GetBuf(L"t", L"w", Base::ST_WCHAR);
    MG_CHECK(values && values->GetAsWStr(0) == L"payload" && values->GetAsWStr(1) == L"ж");

    // The record writer emits the same framing for every column when column compression is requested.
    Base::CBuf plain;
    storage.Save(plain);
    plain.Pointer(12);
    Base::CStorageRecord record(nullptr);
    MG_CHECK(record.Load(plain));
    Base::CBuf written;
    record.Save(written, true);
    const auto bytes = from_buf(written);
    const Bytes prefix = Bytes().utf16z(u"t").u32(1).utf16z(u"w").u32(Base::ST_WCHAR | compressed_column);
    MG_CHECK(bytes.size() > prefix.size() + 4);
    MG_CHECK(std::equal(prefix.data.begin(), prefix.data.end(), bytes.begin()));
    const uint32_t length = wire::read_u32(bytes, prefix.size());
    MG_CHECK(prefix.size() + 4 + length == bytes.size());
    const auto [sizes, decoded] = decode_zl03(std::span<const uint8_t>(bytes).subspan(prefix.size() + 4));
    MG_CHECK(sizes.size() == 1 && decoded == text.data);
}

// --- Malformed storage ------------------------------------------------------------------------------------

void storage_truncated() {
    // Every proper prefix of uncompressed fixtures is rejected without reading past the input.
    for (const Bytes &fixture : {scalar_fixture(), tree_fixture(tree_records)}) {
        for (size_t length = 0; length < fixture.data.size(); ++length) {
            Base::CStorage storage;
            MG_CHECK(load(storage, std::span<const uint8_t>(fixture.data).first(length)) != Load::accepted);
        }
    }
    // A storage object that rejected input remains usable for a later valid load.
    Base::CStorage storage;
    MG_CHECK(load(storage, std::span<const uint8_t>(scalar_fixture().data).first(40)) != Load::accepted);
    MG_CHECK(load(storage, scalar_fixture().data) == Load::accepted);
    check_scalar_values(storage);
}

void storage_malformed() {
    const auto check = [](const Bytes &bytes, Load expected) {
        Base::CStorage storage;
        MG_CHECK(load(storage, bytes.data) == expected);
    };
    // Header: tag and version.
    check(Bytes().text("STRH").u32(0).u32(0), Load::rejected);
    check(Bytes().u32(storage_tag).u32(2).u32(0), Load::rejected);
    check(Bytes().u32(storage_tag).u32(0xffffffff).u32(0), Load::rejected);
    // Version one with a ZL03 body shorter than its 8-byte header, another tag, or no blocks.
    check(Bytes().u32(storage_tag).u32(1).text("ZL03"), Load::exception);
    check(Bytes().u32(storage_tag).u32(1).text("ZL04").u32(1), Load::exception);
    check(Bytes().u32(storage_tag).u32(1).text("ZL03").u32(0), Load::exception);
    // A block with a wrong Adler-32 checksum.
    Bytes corrupt = compressed_storage(scalar_fixture(), 65000);
    corrupt.data.back() ^= 1;
    check(corrupt, Load::exception);
    // Each block decodes to at most 65,000 bytes.
    check(compressed_storage(byte_storage_fixture(64952), 65000), Load::accepted);
    check(compressed_storage(byte_storage_fixture(64953), 65001), Load::exception);
    // Counts larger than the remaining input.
    check(Bytes().u32(storage_tag).u32(0).u32(4096), Load::exception);
    check(Bytes().u32(storage_tag).u32(0).u32(1).utf16z(u"t").u32(4096), Load::exception);
    // A compressed column with a wrong inner tag is the reader's only rejected-body result.
    Bytes column_tag;
    column_tag.u32(storage_tag).u32(0).u32(1).utf16z(u"t").u32(1);
    column(column_tag, u"v", Base::ST_INT32 | compressed_column, Bytes().text("ZL04").u32(1));
    Base::CStorage storage;
    MG_CHECK(load(storage, column_tag.data) == Load::rejected);
    MG_CHECK(!storage.IsTablePresent(L"t"));
}

// --- Packages -------------------------------------------------------------------------------------------

constexpr uint32_t file_record_size = 158;
constexpr uint32_t folder_header_size = 12;

struct Entry {
    std::string name;  // uppercase lookup name
    std::string real_name;
    uint32_t type = Base::FILEEC_BINARY;
    uint32_t new_type = Base::FILEEC_BINARY;
    uint32_t free = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t real_size = 0;
    uint32_t extra = 0;
};

// Folder header and 158-byte records: size, real size, two 63-byte names, type, new type, free flag,
// date, absolute offset and a runtime field that readers ignore. A larger declared record size pads each
// record so that only the size check can reject it.
Bytes folder(const std::vector<Entry> &entries, uint32_t record_size = file_record_size,
             uint32_t declared_count = 0) {
    Bytes out;
    const uint32_t count = declared_count ? declared_count : static_cast<uint32_t>(entries.size());
    out.u32(folder_header_size + count * record_size).u32(count).u32(record_size);
    for (const auto &entry : entries) {
        const uint32_t start = out.size();
        out.u32(entry.size).u32(entry.real_size).fixed(entry.name, 63).fixed(entry.real_name, 63);
        out.u32(entry.type).u32(entry.new_type).u32(entry.free).u32(0x5a5a5a5a).u32(entry.offset).u32(entry.extra);
        MG_CHECK(out.size() - start == file_record_size);
        if (record_size > file_record_size) out.fixed("", record_size - file_record_size);
    }
    return out;
}

// Stored file data: a 32-bit prefix (stored size minus four) followed by the payload.
Entry stored_file(Bytes &pack, std::string name, std::string real_name, const Bytes &content,
                  uint32_t type = Base::FILEEC_BINARY) {
    Entry entry{std::move(name), std::move(real_name), type, type};
    entry.offset = pack.size();
    entry.size = content.size() + 4;
    entry.real_size = content.size();
    pack.u32(content.size()).raw(content);
    return entry;
}

// Compressed file data: the prefix, then blocks of a 32-bit length, "ZL02", the decoded length and a zlib
// stream. Every block except the last decodes to 65,536 bytes.
Entry compressed_file(Bytes &pack, std::string name, std::string real_name, const Bytes &content) {
    Bytes blocks;
    for (size_t offset = 0; offset < content.data.size(); offset += 65536) {
        const auto part = std::span<const uint8_t>(content.data).subspan(
                offset, std::min<size_t>(65536, content.data.size() - offset));
        const Bytes stream = wire::zlib_stored(part);
        blocks.u32(stream.size() + 8).text("ZL02").u32(static_cast<uint32_t>(part.size())).raw(stream);
    }
    Entry entry{std::move(name), std::move(real_name), Base::FILEEC_COMPRESSED, Base::FILEEC_TEXT};
    entry.offset = pack.size();
    entry.size = blocks.size() + 4;
    entry.real_size = content.size();
    entry.extra = 0xdeadbeef;
    pack.u32(blocks.size()).raw(blocks);
    return entry;
}

Entry folder_entry(Bytes &pack, std::string name, std::string real_name, const Bytes &contents) {
    Entry entry{std::move(name), std::move(real_name), Base::FILEEC_FOLDER, Base::FILEEC_FOLDER};
    entry.offset = pack.size();
    entry.size = contents.size();
    entry.extra = 0xdeadbeef;
    pack.raw(contents);
    return entry;
}

Bytes pattern(uint32_t size, uint32_t multiplier) {
    Bytes out;
    for (uint32_t i = 0; i < size; ++i) out.u8(static_cast<uint8_t>(i * multiplier + i / 251));
    return out;
}

const std::string long_name = std::string(58, 'N') + ".BIN";  // 62 characters and a terminator

std::string lower(std::string value) {
    for (auto &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

struct TreePackage {
    Bytes bytes;
    Entry packed;
};

TreePackage tree_package() {
    Bytes pack;
    pack.u32(0);  // root folder offset, patched below
    const Entry readme = stored_file(pack, "README.TXT", "ReadMe.txt", Bytes().text("Synthetic text\r\n"),
                                     Base::FILEEC_TEXT);
    const Entry data = stored_file(pack, "DATA.BIN", "data.bin", pattern(300, 7));
    const Entry longest = stored_file(pack, long_name, lower(long_name), Bytes().text("deep"));
    const Entry packed = compressed_file(pack, "PACKED.BIN", "Packed.bin", pattern(65546, 13));
    const Entry deeper = folder_entry(pack, "DEEPER", "Deeper", folder({longest}));
    const Entry sub = folder_entry(pack, "SUB", "Sub", folder({data, deeper}));
    // A free record is ignored even when it claims to be a folder with an invalid offset.
    Entry ghost{"GHOST.TXT", "Ghost.txt", Base::FILEEC_FOLDER, Base::FILEEC_FOLDER, 1, 0xfffffff0};
    pack.put_u32(0, pack.size());
    pack.raw(folder({readme, ghost, sub, packed}));
    return {pack, packed};
}

class TemporaryFile {
    std::filesystem::path path_;

public:
    explicit TemporaryFile(std::span<const uint8_t> bytes, const std::string &suffix = ".pkg") {
        static int counter;
        path_ = std::filesystem::current_path() / ("matrixgame-layout-" + std::to_string(GetCurrentProcessId()) +
                                                   "-" + std::to_string(counter++) + suffix);
        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        MG_CHECK(file.good());
    }
    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;
    ~TemporaryFile() {
        // Removal fails while a reader still holds the file open.
        if (!DeleteFileW(path_.c_str())) {
            std::cerr << "Cannot remove a synthetic package; a handle may remain open\n";
            std::abort();
        }
    }
    const std::filesystem::path &path() const { return path_; }
};

bool exclusively_openable(const std::filesystem::path &path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

std::vector<uint8_t> read_all(Base::CPackFile &pack, DWORD handle) {
    std::vector<uint8_t> bytes(pack.GetSize(handle));
    MG_CHECK(pack.Read(handle, bytes.data(), static_cast<int>(bytes.size())));
    return bytes;
}

struct Listed {
    bool directory;
    bool compressed;
    std::string name;
    bool operator==(const Listed &) const = default;
};
std::vector<Listed> listed;

bool list_name(bool directory, bool compressed, const std::string &name) {
    listed.push_back({directory, compressed, name});
    return true;
}

void package_tree() {
    const auto [bytes, packed_entry] = tree_package();
    TemporaryFile file(bytes.data);
    {
        Base::CPackFile pack(nullptr, file.path().c_str());
        MG_CHECK(pack.OpenPacketFile());

        // Lookup splits on either separator and compares uppercase request components with stored names.
        for (const char *name : {"readme.txt", "README.TXT", "sub\\data.bin", "SUB/Data.Bin"}) {
            MG_CHECK(pack.FileExists(name));
        }
        MG_CHECK(pack.FileExists("sub/deeper/" + long_name));
        MG_CHECK(!pack.FileExists("ghost.txt") && !pack.FileExists("sub") && !pack.FileExists("missing"));
        MG_CHECK(!pack.FileExists("readme.txt/extra"));
        MG_CHECK(pack.PathExists("sub") && pack.PathExists("Sub\\Deeper") && !pack.PathExists("ghost.txt"));

        const DWORD readme = pack.Open("readme.txt");
        MG_CHECK(readme != 0xffffffff && pack.GetSize(readme) == 16);
        const auto text = read_all(pack, readme);
        MG_CHECK(std::string(text.begin(), text.end()) == "Synthetic text\r\n");
        MG_CHECK(pack.Close(readme));

        const auto expected = pattern(300, 7).data;
        const DWORD data = pack.Open("sub/data.bin");
        MG_CHECK(data != 0xffffffff && pack.GetSize(data) == 300 && pack.GetPos(data) == 0);
        std::vector<uint8_t> part(100);
        MG_CHECK(pack.Read(data, part.data(), 100));
        MG_CHECK(std::equal(part.begin(), part.end(), expected.begin()) && pack.GetPos(data) == 100);
        MG_CHECK(pack.SetPos(data, 50, FILE_CURRENT) && pack.GetPos(data) == 150);
        MG_CHECK(pack.Read(data, part.data(), 10));
        MG_CHECK(std::equal(part.begin(), part.begin() + 10, expected.begin() + 150));
        MG_CHECK(pack.SetPos(data, 10, FILE_END) && pack.GetPos(data) == 290);
        MG_CHECK(pack.Read(data, part.data(), 10));
        MG_CHECK(std::equal(part.begin(), part.begin() + 10, expected.begin() + 290));
        MG_CHECK(pack.Close(data) && !pack.Close(data));

        const DWORD deep = pack.Open("SUB\\DEEPER\\" + long_name);
        const auto deep_bytes = read_all(pack, deep);
        MG_CHECK(std::string(deep_bytes.begin(), deep_bytes.end()) == "deep");
        MG_CHECK(pack.Close(deep));

        const auto packed_expected = pattern(65546, 13).data;
        const DWORD packed = pack.Open("packed.bin");
        MG_CHECK(packed != 0xffffffff && pack.GetSize(packed) == 65546);
        MG_CHECK(read_all(pack, packed) == packed_expected);
        MG_CHECK(pack.SetPos(packed, 6, FILE_END) && pack.GetPos(packed) == 65540);
        MG_CHECK(pack.Read(packed, part.data(), 6));
        MG_CHECK(std::equal(part.begin(), part.begin() + 6, packed_expected.begin() + 65540));
        MG_CHECK(pack.Close(packed));

        MG_CHECK(pack.DecompressedFileSize("packed.bin") == 65546);
        MG_CHECK(pack.CompressedFileSize("packed.bin") == packed_entry.size - 4);
        MG_CHECK(pack.DecompressedFileSize("sub/data.bin") == 300);
        MG_CHECK(pack.DecompressedFileSize("missing") == 0xffffffff && pack.DecompressedFileSize("sub") == 0xffffffff);
        MG_CHECK(pack.Open("missing") == 0xffffffff && pack.Open("ghost.txt") == 0xffffffff);

        // Listings report real names, replace the stored new-type field with the type and skip free records.
        listed.clear();
        pack.ListFileNames(list_name);
        const std::vector<Listed> expected_list = {
            {false, false, "ReadMe.txt"}, {true, false, "Sub"},   {false, false, "data.bin"},
            {true, false, "Deeper"},      {false, false, lower(long_name)}, {true, false, ".."},
            {true, false, ".."},          {false, true, "Packed.bin"},
        };
        MG_CHECK(listed == expected_list);

        Base::SSearchRec search(nullptr);
        MG_CHECK(pack.FindFirst("", 0, search) == 0 && search.Name == "ReadMe.txt" && search.T == Base::FILEEC_TEXT);
        MG_CHECK(pack.FindNext(search) == 0 && search.Name == "Sub" && search.T == Base::FILEEC_FOLDER);
        MG_CHECK(pack.FindNext(search) == 0 && search.Name == "Packed.bin" && search.T == Base::FILEEC_COMPRESSED);
        MG_CHECK(pack.FindNext(search) == 1);
        MG_CHECK(pack.FindFirst("sub", 0, search) == 0 && search.Name == "data.bin");
        MG_CHECK(pack.FindNext(search) == 0 && search.Name == "Deeper" && pack.FindNext(search) == 1);
        pack.FindClose(search);

        MG_CHECK(pack.ClosePacketFile());
        MG_CHECK(pack.GetHandle() == 0xffffffff && pack.m_RootFolder == nullptr);
        MG_CHECK(exclusively_openable(file.path()));
    }
}

Bytes simple_package(const std::vector<std::pair<std::string, std::string>> &files) {
    Bytes pack;
    pack.u32(0);
    std::vector<Entry> entries;
    for (const auto &[name, content] : files) {
        entries.push_back(stored_file(pack, name, lower(name), Bytes().text(content)));
    }
    pack.put_u32(0, pack.size());
    return pack.raw(folder(entries));
}

std::string read_text(Base::CPackCollection &packs, DWORD handle) {
    std::string text(packs.GetSize(handle), '\0');
    MG_CHECK(packs.Read(handle, text.data(), static_cast<int>(text.size())));
    return text;
}

void package_handles() {
    TemporaryFile first(simple_package({{"README.TXT", "first"}}).data);
    TemporaryFile second(simple_package({{"README.TXT", "second"}, {"ONLY.TXT", "only"}}).data);
    {
        // Each package has 16 virtual handle slots; the lowest free slot is reused.
        Base::CPackFile pack(nullptr, first.path().c_str());
        MG_CHECK(pack.OpenPacketFile());
        for (DWORD i = 0; i < 16; ++i) MG_CHECK(pack.Open("readme.txt") == i);
        MG_CHECK(pack.Open("readme.txt") == 0xffffffff);
        MG_CHECK(pack.Close(5) && pack.Open("readme.txt") == 5);
        // Closing the package also closes its open virtual handles.
        MG_CHECK(pack.ClosePacketFile() && exclusively_openable(first.path()));
    }
    {
        // A collection searches packages in registration order; a handle is slot + (package index << 4).
        Base::CPackCollection packs(nullptr);
        packs.AddPacketFile(first.path().c_str());
        packs.AddPacketFile(second.path().c_str());
        MG_CHECK(packs.OpenPacketFiles());
        MG_CHECK(packs.FileExists("only.txt") && !packs.FileExists("missing.txt"));
        const DWORD shadowed = packs.Open("readme.txt");
        const DWORD only = packs.Open("only.txt");
        MG_CHECK(shadowed == 0 && only == 16);
        MG_CHECK(read_text(packs, shadowed) == "first" && read_text(packs, only) == "only");
        MG_CHECK(packs.Close(shadowed) && packs.Close(only));
        MG_CHECK(packs.ClosePacketFiles());
    }
    MG_CHECK(exclusively_openable(first.path()) && exclusively_openable(second.path()));

    // One malformed package rejects the collection and closes packages that were already open.
    TemporaryFile malformed(Bytes().u32(4096).data);
    Base::CPackCollection packs(nullptr);
    packs.AddPacketFile(first.path().c_str());
    packs.AddPacketFile(malformed.path().c_str());
    MG_CHECK(!packs.OpenPacketFiles());
    MG_CHECK(exclusively_openable(first.path()) && exclusively_openable(malformed.path()));
}

void check_rejected_package(const Bytes &bytes) {
    TemporaryFile file(bytes.data);
    Base::CPackFile pack(nullptr, file.path().c_str());
    MG_CHECK(!pack.OpenPacketFile());
    MG_CHECK(pack.GetHandle() == 0xffffffff && pack.m_RootFolder == nullptr);
    MG_CHECK(exclusively_openable(file.path()));
}

Bytes package_with_root(const Bytes &root) {
    Bytes pack;
    pack.u32(4);
    return pack.raw(root);
}

void package_malformed() {
    const Entry file{"A.TXT", "a.txt", Base::FILEEC_BINARY, Base::FILEEC_BINARY, 0, 4, 5, 1};
    check_rejected_package(Bytes());
    check_rejected_package(Bytes().u8(4).u8(0).u8(0));
    check_rejected_package(Bytes().u32(4096));              // root folder beyond the end
    check_rejected_package(Bytes().u32(4).u32(12).u32(0));  // truncated folder header
    for (uint32_t size : {157u, 159u, 166u}) {              // record size other than 158
        check_rejected_package(package_with_root(folder({file}, size)));
    }
    check_rejected_package(package_with_root(folder({file}, file_record_size, 3)));  // missing records

    // A nested folder outside the file, with a wrong record size or with missing records.
    Entry nested{"SUB", "Sub", Base::FILEEC_FOLDER, Base::FILEEC_FOLDER, 0, 0x100000};
    check_rejected_package(package_with_root(folder({file, nested})));
    // The nested folder is last, so a short read cannot continue into other structures.
    const Entry sub{"SUB", "Sub", Base::FILEEC_FOLDER, Base::FILEEC_FOLDER, 0,
                    4 + folder_header_size + 2 * file_record_size};
    for (const Bytes &contents : {folder({file}, 166), folder({file}, file_record_size, 2)}) {
        check_rejected_package(package_with_root(folder({file, sub})).raw(contents));
    }

    // An empty root folder is valid and contains nothing.
    TemporaryFile empty(package_with_root(folder({})).data);
    Base::CPackFile pack(nullptr, empty.path().c_str());
    MG_CHECK(pack.OpenPacketFile());
    MG_CHECK(!pack.FileExists("a.txt") && pack.Open("a.txt") == 0xffffffff);
    MG_CHECK(pack.ClosePacketFile());
}

class WorkingDirectory {
    std::filesystem::path previous_ = std::filesystem::current_path();
    std::filesystem::path path_;

public:
    WorkingDirectory() {
        path_ = previous_ / ("matrixgame-layout-" + std::to_string(GetCurrentProcessId()));
        MG_CHECK(std::filesystem::create_directory(path_));
        std::filesystem::current_path(path_);
    }
    WorkingDirectory(const WorkingDirectory &) = delete;
    WorkingDirectory &operator=(const WorkingDirectory &) = delete;
    ~WorkingDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
        std::filesystem::remove_all(path_, error);
        if (error) std::abort();
    }
};

struct RegisteredPackages {
    ~RegisteredPackages() { Base::CFile::ReleasePackFiles(); }
};

void write_file(const std::filesystem::path &path, const Bytes &bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data.data()), static_cast<std::streamsize>(bytes.size()));
    MG_CHECK(file.good());
}

void package_file_fallback() {
    WorkingDirectory directory;
    // Packed configuration inside a registered package, read through the production file fallback.
    Bytes pack;
    pack.u32(0);
    const Entry robots = stored_file(pack, "ROBOTS.DAT", "robots.dat", tree_fixture(tree_records));
    const Entry cfg = folder_entry(pack, "CFG", "CFG", folder({robots}));
    pack.put_u32(0, pack.size());
    write_file("layout.pkg", pack.raw(folder({cfg})));
    Base::CBlockPar expected;
    fill_tree(expected);
    {
        RegisteredPackages registered;
        Base::CFile::AddPackFile(L"layout.pkg");
        Base::CFile::OpenPackFiles();
        Base::CStorage packed;
        MG_CHECK(packed.Load(L"cfg\\robots.dat"));
        Base::CBlockPar restored;
        packed.RestoreBlockPar(L"da", restored);
        check_tree(expected, restored);

        // A loose file with the same name overrides the archived file.
        std::filesystem::create_directory("cfg");
        write_file("cfg/robots.dat", scalar_fixture());
        Base::CStorage loose;
        MG_CHECK(loose.Load(L"cfg\\robots.dat"));
        check_scalar_values(loose);
        MG_CHECK(!loose.IsTablePresent(L"da"));
    }
    MG_CHECK(exclusively_openable("layout.pkg"));
}

// --- Raw-read records ------------------------------------------------------------------------------------

// Map payload and vector-object readers cast stored bytes to these structures, so their compiled layout is
// the stored layout. All fields are fixed-width on Windows; SVOEdge has two trailing padding bytes.
static_assert(sizeof(SCompilePoint) == 12 && offsetof(SCompilePoint, z) == 4 && offsetof(SCompilePoint, b) == 8 &&
              offsetof(SCompilePoint, g) == 9 && offsetof(SCompilePoint, r) == 10 &&
              offsetof(SCompilePoint, flags) == 11);
static_assert(sizeof(SCompileMove) == 16 && offsetof(SCompileMove, m_Move) == 4 &&
              offsetof(SCompileMove, m_Sphere) == 8 && offsetof(SCompileMove, m_Zubchik) == 12);
static_assert(sizeof(SCompileMoveCell) == 64);
static_assert(sizeof(SCompileBottomVert) == 8 && offsetof(SCompileBottomVert, y) == 2 &&
              offsetof(SCompileBottomVert, tx) == 4 && offsetof(SCompileBottomVert, ty) == 6);
static_assert(sizeof(SVOHeader) == 80 && offsetof(SVOHeader, m_MaterialCnt) == 16 &&
              offsetof(SVOHeader, m_VerCnt) == 32 && offsetof(SVOHeader, m_FrameCnt) == 48 &&
              offsetof(SVOHeader, m_MatrixCnt) == 64 && offsetof(SVOHeader, m_EdgeSme) == 76);
static_assert(sizeof(SVOVertexNorTex) == 32 && offsetof(SVOVertexNorTex, tu) == 24);
static_assert(sizeof(SVOMaterial) == 136 && offsetof(SVOMaterial, power) == 68 &&
              offsetof(SVOMaterial, tex_diffuse) == 72);
static_assert(sizeof(SVOGroup) == 32 && sizeof(SVOFrame) == 64 && offsetof(SVOFrame, m_EdgeCnt) == 52);
static_assert(sizeof(SVOAnimHeader) == 80 && offsetof(SVOAnimHeader, m_UnitCnt) == 68);
static_assert(sizeof(SVOMatrixHeader) == 80 && offsetof(SVOMatrixHeader, m_MatrixSme) == 68);
static_assert(sizeof(SVOFrameEdge) == 8 && sizeof(SVOEdge) == 12 && offsetof(SVOEdge, m_Edge1) == 8);

// Package records in their current in-memory form. SFolderRec and the 32-bit enum are fixed-width; the
// SFileRec offsets hold only while its two uintptr_t fields are 32-bit (the supported x86 build).
static_assert(sizeof(Base::SFolderRec) == 12 && sizeof(Base::EFileType) == 4);
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(Base::SFileRec) == 158 && offsetof(Base::SFileRec, m_Name) == 8 &&
              offsetof(Base::SFileRec, m_RealName) == 71 && offsetof(Base::SFileRec, m_Type) == 134 &&
              offsetof(Base::SFileRec, m_NType) == 138 && offsetof(Base::SFileRec, m_Free) == 142 &&
              offsetof(Base::SFileRec, m_Date) == 146 && offsetof(Base::SFileRec, m_Offset) == 150 &&
              offsetof(Base::SFileRec, m_Extra) == 154);
#endif

template <class T>
T from_bytes(const Bytes &bytes) {
    MG_CHECK(bytes.size() == sizeof(T));
    T value;
    std::memcpy(&value, bytes.data.data(), sizeof(T));
    return value;
}

void raw_records() {
    // Little-endian integers and IEEE-754 binary32 values, as stored.
    const auto point = from_bytes<SCompilePoint>(Bytes().u32(0x01020304).u32(0x3fc00000).u8(10).u8(11).u8(12).u8(13));
    MG_CHECK(point.move == 0x01020304 && point.z == 1.5f);
    MG_CHECK(point.b == 10 && point.g == 11 && point.r == 12 && point.flags == 13);
    const auto vertex = from_bytes<SCompileBottomVert>(Bytes().u16(1).u16(2).u16(0x0304).u16(0xfffe));
    MG_CHECK(vertex.x == 1 && vertex.y == 2 && vertex.tx == 0x0304 && vertex.ty == 0xfffe);
    const auto move = from_bytes<SCompileMove>(Bytes().u32(0xffffffff).u32(2).u32(3).u32(4));
    MG_CHECK(move.m_Zone == -1 && move.m_Move == 2 && move.m_Sphere == 3 && move.m_Zubchik == 4);
    Bytes header;
    header.text("vo").u16(0);
    for (uint32_t i = 1; i < 20; ++i) header.u32(i);
    const auto vo = from_bytes<SVOHeader>(header);
    MG_CHECK(vo.m_Id == 0x00006f76 && vo.m_Ver == 1 && vo.m_MaterialCnt == 4 && vo.m_EdgeSme == 19);
}

constexpr tests::Case cases[] = {
    {"base.layout.storage_columns", storage_columns},
    {"base.layout.storage_tree", storage_tree},
    {"base.layout.storage_zl03", storage_zl03},
    {"base.layout.storage_column_zl03", storage_column_zl03},
    {"base.layout.storage_truncated", storage_truncated},
    {"base.layout.storage_malformed", storage_malformed},
    {"base.layout.package_tree", package_tree},
    {"base.layout.package_handles", package_handles},
    {"base.layout.package_malformed", package_malformed},
    {"base.layout.package_file_fallback", package_file_fallback},
    {"base.layout.raw_records", raw_records},
};

}  // namespace

int main(int argc, char **argv) {
    // A native crash must terminate the case instead of opening an error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    DTRACE();
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Layout test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
