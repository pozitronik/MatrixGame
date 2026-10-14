// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MapMetadata.hpp"
#include "map_language_fixture.hpp"

#include <zlib.h>
#include <windows.h>

#include <array>
#include <fstream>
#include <vector>

namespace {

void original_format() {
    const auto catalog = MapMetadata::decode(language_fixture);
    MG_CHECK(catalog.size() == 1);
    const auto &entry = catalog.at(L"test.cmap");
    MG_CHECK(entry.name == L"\u0422\u0435\u0441\u0442");
    MG_CHECK(entry.description == L"First paragraph: Pilot.\n\nSecond on Terra, Vega.");
    MG_CHECK(entry.group == 1 && entry.length == 3 && entry.sides == L"Red,Blue");
}

void append32(std::vector<uint8_t> &bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void envelope() {
    std::vector<uint8_t> bytes;
    append32(bytes, sizeof(language_fixture) ^ 0xc83fcbf3u ^ 0x7db6c99du);
    std::vector<uint8_t> content;
    append32(content, static_cast<uint32_t>(crc32(0, language_fixture, sizeof(language_fixture))) ^ 0x7db6c99du);
    content.insert(content.end(), std::begin(language_fixture), std::end(language_fixture));
    append32(bytes, static_cast<uint32_t>(crc32(0, content.data(), content.size())) ^ 0xc83fcbf3u);
    bytes.insert(bytes.end(), std::begin(language_fixture), std::end(language_fixture));
    MG_CHECK(MapMetadata::decode(bytes).at(L"test.cmap").group == 1);
}

void rejected(std::span<const uint8_t> bytes) {
    bool failed = false;
    try { (void)MapMetadata::decode(bytes); }
    catch (const std::runtime_error &) { failed = true; }
    MG_CHECK(failed);
}

void damaged() {
    for (size_t end = 0; end < sizeof(language_fixture); ++end) rejected(std::span(language_fixture).first(end));
    for (size_t at = 0; at < sizeof(language_fixture); ++at) {
        std::vector<uint8_t> bytes(std::begin(language_fixture), std::end(language_fixture));
        bytes[at] ^= 0x80;
        rejected(bytes);
    }
    rejected(std::vector<uint8_t>(16 * 1024 * 1024 + 1));
}

// Encode malformed clear trees with a valid checksum, so rejection must come from the parser.
std::vector<uint8_t> encoded(std::span<const uint8_t> tree, uint32_t claimed = 0,
                             uint32_t key = static_cast<uint32_t>(-1310144887)) {
    std::vector<uint8_t> clear{'Z','L','0','1'};
    append32(clear, claimed ? claimed : static_cast<uint32_t>(tree.size()));
    uLongf length = compressBound(tree.size());
    const size_t start = clear.size();
    clear.resize(start + length);
    MG_CHECK(compress2(clear.data() + start, &length, tree.data(), tree.size(), 9) == Z_OK);
    clear.resize(start + length);
    std::vector<uint8_t> bytes;
    append32(bytes, static_cast<uint32_t>(crc32(0, clear.data(), clear.size())));
    append32(bytes, 12345u ^ key);
    int64_t state = 12345;
    for (auto byte : clear) {
        state = (state * 16807) % 2147483647;
        bytes.push_back(byte ^ static_cast<uint8_t>(state - 1));
    }
    return bytes;
}

using Bytes = std::vector<uint8_t>;

void text(Bytes &bytes, std::wstring_view value) {
    for (auto ch : value) { bytes.push_back(static_cast<uint8_t>(ch)); bytes.push_back(static_cast<uint8_t>(ch >> 8)); }
    bytes.push_back(0); bytes.push_back(0);
}

Bytes value(std::wstring_view name, std::wstring_view content) {
    Bytes result{1}; text(result, name); text(result, content); return result;
}

Bytes block(std::wstring_view name, std::initializer_list<Bytes> children) {
    Bytes result{2}; text(result, name); result.push_back(0); append32(result, children.size());
    for (const auto &child : children) result.insert(result.end(), child.begin(), child.end());
    return result;
}

Bytes tree(Bytes root) { root.erase(root.begin(), root.begin() + 3); return root; }

void catalog_rules() {
    const auto map = block(L"one", {value(L"Map", L"safe.cmap"), value(L"Name", L"Safe")});
    const auto input = tree(block(L"", {block(L"RobotsMap", {map,
        block(L"unsafe", {value(L"Map", L"..\\outside.cmap")})})}));
    for (uint32_t key : {static_cast<uint32_t>(-1310144887), uint32_t(1050086386)}) {
        const auto result = MapMetadata::decode(encoded(input, 0, key));
        MG_CHECK(result.size() == 1 && result.at(L"safe.cmap").name == L"Safe");
        MG_CHECK(result.at(L"safe.cmap").description.empty() && result.at(L"safe.cmap").length == -1);
    }
    rejected(encoded(tree(block(L"", {block(L"RobotsMap", {map, map})}))));
    rejected(encoded(tree(block(L"", {block(L"RobotsMap", {block(L"one", {
        value(L"Map", L"safe.cmap"), value(L"Name", L"one"), value(L"Name", L"two")})})}))));
    auto trailing = input; trailing.push_back(0);
    rejected(encoded(trailing));
    Bytes deep = value(L"leaf", L"value");
    for (int i = 0; i < 66; ++i) deep = block(L"", {deep});
    rejected(encoded(tree(deep)));
}

void malformed_tree() {
    rejected(encoded(std::array<uint8_t, 5>{2,0,0,0,0}));  // Bad sorted flag.
    rejected(encoded(std::array<uint8_t, 5>{0,0xff,0xff,0xff,0xff}));  // Node budget.
    rejected(encoded(std::array<uint8_t, 5>{0,0,0,0,0}));  // Missing catalog.
    rejected(encoded(std::array<uint8_t, 5>{0,0,0,0,0}, 17 * 1024 * 1024));
    rejected(encoded(std::array<uint8_t, 5>{0,0,0,0,0}, 4));  // False decoded size.
    rejected(encoded(std::array<uint8_t, 8>{0,1,0,0,0,1,0,0xd8}));  // Unterminated surrogate name.
}

void plain() {
    MG_CHECK(MapMetadata::plain_text(L"<clr>Title<clrEnd>|_BITMAP:bad:[value]\r\n\ttext") ==
             L"Title/_BITMAP:bad:(value)\n text");
    MG_CHECK(MapMetadata::plain_text(L"before<broken") == L"before");
}

void files() {
    // Keep fixtures in the build directory and remove only the directory created by this test.
    const auto folder = std::filesystem::current_path() /
                        (L"matrix-map-language-" + std::to_wstring(GetCurrentProcessId()));
    MG_CHECK(std::filesystem::create_directory(folder));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{folder};
    const auto path = folder / "Lang.dat";
    std::string error = "old";
    MG_CHECK(MapMetadata::load(error, path).empty() && error.empty());
    { std::ofstream stream(path, std::ios::binary); stream.write(reinterpret_cast<const char *>(language_fixture), sizeof(language_fixture)); }
    MG_CHECK(MapMetadata::load(error, path).size() == 1 && error.empty());
    { std::ofstream stream(path, std::ios::binary); stream << "broken"; }
    MG_CHECK(MapMetadata::load(error, path).empty() && !error.empty());
}

}  // namespace

int main(int argc, char **argv) {
    const tests::Case cases[] = {
        {"game.maps.metadata_format", original_format}, {"game.maps.metadata_envelope", envelope},
        {"game.maps.metadata_damage", damaged}, {"game.maps.metadata_tree", malformed_tree},
        {"game.maps.metadata_text", plain}, {"game.maps.metadata_files", files},
        {"game.maps.metadata_catalog", catalog_rules},
    };
    return tests::run(argc, argv, cases);
}
