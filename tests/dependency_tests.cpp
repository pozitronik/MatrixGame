// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "dependency_fixtures.hpp"

#include "CStorage.hpp"
#include "CBitmap.hpp"
#include "FilePNG.hpp"
#include "Pack.hpp"
#include "stupid_logger.hpp"

#include <png.h>
#include <zlib.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <vector>

logger_type lgr{std::cerr};

namespace {

void linked_versions() {
    // Catch a reused build linking a stale archive against updated headers.
    MG_CHECK(std::string_view(zlibVersion()) == ZLIB_VERSION);
    MG_CHECK(png_access_version_number() == PNG_LIBPNG_VER);
}

void legacy_configuration() {
    Base::CBuf bytes;
    bytes.Add(dependency_fixtures::configuration.data(), dependency_fixtures::configuration.size());
    Base::CStorage storage;
    MG_CHECK(storage.Load(bytes));
    Base::CBlockPar tree;
    storage.RestoreBlockPar(L"da", tree);
    MG_CHECK(tree.ParCount() == 3);
    MG_CHECK(tree.ParGetName(0) == L"duplicate" && tree.ParGet(0) == L"first");
    MG_CHECK(tree.ParGetName(1) == L"duplicate" && tree.ParGet(1) == L"second");
    MG_CHECK(tree.ParGet(L"unicode") == L"\u041f\u0440\u0438\u0432\u0435\u0442");
    MG_CHECK(tree.BlockCount() == 1 && tree.BlockGetName(0) == L"child");
    MG_CHECK(tree.BlockGet(0)->ParGetName(0) == L"empty");
    MG_CHECK(tree.BlockGet(0)->ParGet(0).empty());
}

void multiblock_configuration() {
    const std::wstring value(100003, L'\u044f');
    Base::CBlockPar tree;
    tree.ParAdd(L"large", value);
    Base::CStorage storage;
    storage.StoreBlockPar(L"da", tree);
    Base::CBuf bytes;
    storage.Save(bytes, true);
    MG_CHECK(bytes.Len() > 20);
    MG_CHECK(std::memcmp(bytes.Buff<unsigned char>() + 8, "ZL03", 4) == 0);
    uint32_t blocks;
    std::memcpy(&blocks, bytes.Buff<unsigned char>() + 12, sizeof(blocks));
    MG_CHECK(blocks > 1);
    Base::CStorage loaded;
    MG_CHECK(loaded.Load(bytes));
    Base::CBlockPar restored;
    loaded.RestoreBlockPar(L"da", restored);
    MG_CHECK(restored.ParGet(L"large") == value);
}

void put_u32(std::vector<unsigned char> &bytes, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.at(offset++) = static_cast<unsigned char>(value >> shift);
    }
}

void append_u32(std::vector<unsigned char> &bytes, uint32_t value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + 4);
    put_u32(bytes, offset, value);
}

void append_pack_block(std::vector<unsigned char> &bytes, std::span<const unsigned char> compressed,
                       uint32_t decoded_size) {
    append_u32(bytes, static_cast<uint32_t>(compressed.size()) + 8);
    bytes.insert(bytes.end(), {'Z', 'L', '0', '2'});
    append_u32(bytes, decoded_size);
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
}

class TemporaryPack {
    std::filesystem::path path_;

public:
    explicit TemporaryPack(std::span<const unsigned char> bytes)
      : path_(std::filesystem::current_path() /
              ("matrixgame-dependency-" + std::to_string(GetCurrentProcessId()) + ".pkg")) {
        const HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        MG_CHECK(file != INVALID_HANDLE_VALUE);
        DWORD written = 0;
        const BOOL success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(file);
        if (!success || written != bytes.size()) {
            DeleteFileW(path_.c_str());
            throw std::runtime_error("Cannot write synthetic package");
        }
    }
    ~TemporaryPack() {
        if (!DeleteFileW(path_.c_str())) {
            std::cerr << "Cannot remove synthetic package\n";
            std::abort();
        }
    }
    const std::filesystem::path &path() const { return path_; }
};

void legacy_pack_blocks() {
    // Fixed little-endian x86 metadata, with two frozen zlib 1.2.11 ZL02 blocks.
    static_assert(sizeof(Base::SFileRec) == 158);
    std::vector<unsigned char> bytes(174, 0);
    put_u32(bytes, 0, 4);
    put_u32(bytes, 4, 170);
    put_u32(bytes, 8, 1);
    put_u32(bytes, 12, 158);
    put_u32(bytes, 20, 65536 + 17);
    constexpr char name[] = "PAYLOAD.BIN";
    std::memcpy(bytes.data() + 24, name, sizeof(name));
    std::memcpy(bytes.data() + 87, name, sizeof(name));
    put_u32(bytes, 150, Base::FILEEC_COMPRESSED);
    put_u32(bytes, 154, Base::FILEEC_COMPRESSED);
    put_u32(bytes, 166, 174);
    append_u32(bytes, 0);  // file block size
    append_pack_block(bytes, dependency_fixtures::pack_block, 65536);
    append_pack_block(bytes, dependency_fixtures::pack_tail, 17);
    put_u32(bytes, 16, static_cast<uint32_t>(bytes.size()) - 174);
    put_u32(bytes, 174, static_cast<uint32_t>(bytes.size()) - 178);

    TemporaryPack file(bytes);
    Base::CPackFile pack(nullptr, file.path().c_str());
    MG_CHECK(pack.OpenPacketFile());
    const DWORD handle = pack.Open("payload.bin");
    MG_CHECK(handle != 0xffffffff);
    MG_CHECK(pack.GetSize(handle) == 65553);
    std::vector<unsigned char> decoded(65553);
    MG_CHECK(pack.Read(handle, decoded.data(), static_cast<int>(decoded.size())));
    for (size_t i = 0; i < decoded.size(); ++i) {
        const size_t block_index = i < 65536 ? i : i - 65536;
        MG_CHECK(decoded[i] == block_index % 251);
    }
    MG_CHECK(pack.SetPos(handle, 65530));
    std::array<unsigned char, 20> crossing{};
    MG_CHECK(pack.Read(handle, crossing.data(), crossing.size()));
    MG_CHECK(std::memcmp(crossing.data(), decoded.data() + 65530, crossing.size()) == 0);
    MG_CHECK(pack.GetPos(handle) == 65550);
    MG_CHECK(pack.Close(handle));
    MG_CHECK(pack.ClosePacketFile());
}

void check_png(std::span<const unsigned char> fixture, unsigned channels) {
    std::vector<unsigned char> encoded(fixture.begin(), fixture.end());
    CBitmap image;
    MG_CHECK(image.LoadFromPNG(encoded.data(), static_cast<int>(encoded.size())));
    MG_CHECK(image.SizeX() == 2 && image.SizeY() == 2);
    MG_CHECK(image.BytePP() == static_cast<int>(channels));
    for (unsigned y = 0; y < 2; ++y) {
        for (unsigned x = 0; x < 2 * channels; ++x) {
            MG_CHECK(image.Data()[y * image.Pitch() + x] == ((y * 2 * channels + x) * 37 + 11) % 256);
        }
    }
}

void png_gray() { check_png(dependency_fixtures::png_gray, 1); }
void png_rgb() { check_png(dependency_fixtures::png_rgb, 3); }
void png_rgba() { check_png(dependency_fixtures::png_rgba, 4); }

void png_write_round_trip() {
    // RGB rows have padding; BGR conversion must not consume the padding bytes.
    std::array<unsigned char, 16> pixels = {1, 2, 3, 4, 5, 6, 199, 198, 7, 8, 9, 10, 11, 12, 197, 196};
    std::array<unsigned char, 2048> encoded{};
    const int size = FilePNG::Write(encoded.data(), encoded.size(), pixels.data(), 8, 2, 2, 3, 1);
    MG_CHECK(size > 0 && size < static_cast<int>(encoded.size()));
    CBitmap image;
    MG_CHECK(image.LoadFromPNG(encoded.data(), size));
    MG_CHECK(image.BytePP() == 3 && image.SizeX() == 2 && image.SizeY() == 2);
    constexpr std::array<unsigned char, 12> expected = {3, 2, 1, 6, 5, 4, 9, 8, 7, 12, 11, 10};
    for (unsigned y = 0; y < 2; ++y) {
        MG_CHECK(std::memcmp(image.Data() + y * image.Pitch(), expected.data() + y * 6, 6) == 0);
    }
}

void png_invalid() {
    std::vector<unsigned char> bytes(dependency_fixtures::png_rgb.begin(), dependency_fixtures::png_rgb.end());
    CBitmap image;
    MG_CHECK(!image.LoadFromPNG(bytes.data(), 12));
    MG_CHECK(image.Data() == nullptr);
    bytes[29] ^= 1;  // IHDR checksum
    MG_CHECK(!image.LoadFromPNG(bytes.data(), static_cast<int>(bytes.size())));
    MG_CHECK(image.Data() == nullptr);
}

constexpr tests::Case cases[] = {
    {"base.dependencies.linked_versions", linked_versions},
    {"base.dependencies.legacy_configuration", legacy_configuration},
    {"base.dependencies.multiblock_configuration", multiblock_configuration},
    {"base.dependencies.legacy_pack_blocks", legacy_pack_blocks},
    {"bitmap.png.legacy_gray", png_gray},
    {"bitmap.png.legacy_rgb", png_rgb},
    {"bitmap.png.legacy_rgba", png_rgba},
    {"bitmap.png.write_round_trip", png_write_round_trip},
    {"bitmap.png.invalid", png_invalid},
};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    DTRACE();
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Dependency test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
