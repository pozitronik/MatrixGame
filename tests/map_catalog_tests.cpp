// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MapCatalog.hpp"
#include "stupid_logger.hpp"

logger_type lgr{std::cerr};

namespace {

using MapCatalog::Map;

void merging() {
    const std::vector<std::wstring> packed{L"Desert.cmap", L"desert.JPG", L"Beta.cmap",  L"beta.png",
                                           L"beta.jpg",    L"Gamma.cmap", L"lonely.jpg", L"Alpha.cmap",
                                           L"ALPHA.cmap",  L"notes.txt",  L"plain"};
    const std::vector<std::wstring> loose{L"DESERT.cmap", L"Delta.cmap", L"delta.png", L"Gamma.JPG"};
    const std::vector<Map> expected{
            {L"Alpha", L"Matrix\\Map\\Alpha.cmap", L"", false},  // The first packaged spelling wins.
            {L"Beta", L"Matrix\\Map\\Beta.cmap", L"Matrix\\Map\\beta.jpg", false},  // JPEG before PNG.
            {L"Delta", L"Matrix\\Map\\Delta.cmap", L"Matrix\\Map\\delta.png", true},
            {L"DESERT", L"Matrix\\Map\\DESERT.cmap", L"Matrix\\Map\\desert.JPG", true},  // Loose overrides.
            {L"Gamma", L"Matrix\\Map\\Gamma.cmap", L"Matrix\\Map\\Gamma.JPG", false},
    };
    const auto maps = MapCatalog::merge(packed, loose);
    MG_CHECK(maps == expected);
    MG_CHECK(MapCatalog::merge({}, {}).empty());
    MG_CHECK(MapCatalog::merge({L"only.jpg", L"only.png"}, {}).empty());

    // Order is case-insensitive and independent of the source order.
    const auto reversed = MapCatalog::merge({L"b.cmap", L"A.cmap", L"c.cmap"}, {});
    MG_CHECK(reversed.size() == 3 && reversed[0].name == L"A" && reversed[1].name == L"b" && reversed[2].name == L"c");

    // A loose preview replaces a packaged one with the same extension.
    const auto preview = MapCatalog::merge({L"m.cmap", L"m.jpg"}, {L"M.JPG"});
    MG_CHECK(preview.size() == 1 && preview[0].preview == L"Matrix\\Map\\M.JPG" && !preview[0].loose);
}

void unsafe_names() {
    // Names that the settings file or hint markup would misread are not offered.
    const auto maps = MapCatalog::merge({L"a|b.cmap", L"a=b.cmap", L"a[b].cmap", L"a{b}.cmap", L"caf\u00e9.cmap",
                                         L".cmap", L"ok (2).cmap"},
                                        {L"tab\t.cmap"});
    MG_CHECK(maps.size() == 1 && maps[0].name == L"ok (2)");
}

void external_folders() {
    const auto maps = MapCatalog::merge({L"Shared.cmap", L"shared.jpg", L"Packed.cmap"},
                                        {L"SHARED.cmap", L"shared.JPG", L"Legacy.cmap"},
                                        {L"shared.cmap", L"SHARED.jpg", L"Root.cmap", L"Root.png"},
                                        {L"Shared.CMAP", L"Shared.jpg", L"Data.cmap", L"Data.png"});
    const std::vector<Map> expected{
            {L"Data", L"DATA\\maps\\Data.cmap", L"DATA\\maps\\Data.png", true},
            {L"Legacy", L"Matrix\\Map\\Legacy.cmap", L"", true},
            {L"Packed", L"Matrix\\Map\\Packed.cmap", L"", false},
            {L"Root", L"maps\\Root.cmap", L"maps\\Root.png", true},
            {L"shared", L"maps\\shared.cmap", L"maps\\SHARED.jpg", true},
    };
    MG_CHECK(maps == expected);
    MG_CHECK(MapCatalog::find(maps, L"MAPS\\SHARED.CMAP") == 4);
    MG_CHECK(MapCatalog::find(maps, L"Matrix\\Map\\Shared.cmap") == -1);

    // Each lower-priority source remains available when the source above it is absent.
    const auto data = MapCatalog::merge({L"a.cmap"}, {L"A.cmap"}, {}, {L"a.CMAP"});
    MG_CHECK(data.size() == 1 && data[0].path == L"DATA\\maps\\a.CMAP");
    const auto legacy = MapCatalog::merge({L"a.cmap"}, {L"A.cmap"});
    MG_CHECK(legacy.size() == 1 && legacy[0].path == L"Matrix\\Map\\A.cmap");

    // Preview-only files can supplement a map in another source. JPEG retains precedence over PNG.
    const auto preview = MapCatalog::merge({L"a.cmap", L"a.jpg", L"b.cmap", L"b.png"}, {},
                                           {L"a.png", L"b.PNG"}, {L"a.JPG", L"b.png"});
    MG_CHECK(preview.size() == 2 && preview[0].preview == L"DATA\\maps\\a.JPG");
    MG_CHECK(preview[1].preview == L"maps\\b.PNG" && !preview[1].loose);
    MG_CHECK(MapCatalog::merge({}, {}, {L"bad=name.cmap"}, {L"bad|name.cmap"}).empty());
}

void paths() {
    for (const wchar_t *path : {L"Matrix\\Map\\desert1_3e.cmap", L"Matrix/Map/x.CMAP", L"maps\\a b.cmap",
                                L"x.cmap", L"Matrix\\Map\\v1.2-beta(+).cmap"})
        MG_CHECK(MapCatalog::valid_path(path));
    for (const wchar_t *path : {L"", L".cmap", L"x.cmp", L"x.cmap.txt", L"\\Matrix\\x.cmap", L"/x.cmap",
                                L"C:\\x.cmap", L"..\\x.cmap", L"Matrix\\..\\x.cmap", L"Matrix\\.\\x.cmap",
                                L"Matrix\\\\x.cmap", L"Matrix//x.cmap", L"x.cmap ", L" x.cmap", L"a|b.cmap",
                                L"a=b.cmap", L"a//b.cmap", L"\u0434.cmap", L"a{b.cmap", L"a[b].cmap"})
        MG_CHECK(!MapCatalog::valid_path(path));

    const auto maps = MapCatalog::merge({L"One.cmap", L"two.cmap"}, {});
    MG_CHECK(MapCatalog::find(maps, L"matrix\\map\\ONE.cmap") == 0);
    MG_CHECK(MapCatalog::find(maps, L"Matrix\\Map\\two.cmap") == 1);
    MG_CHECK(MapCatalog::find(maps, L"Matrix\\Map\\three.cmap") == -1 && MapCatalog::find({}, L"x.cmap") == -1);
}

constexpr tests::Case cases[] = {
        {"game.maps.merge", merging},
        {"game.maps.external_folders", external_folders},
        {"game.maps.unsafe_names", unsafe_names},
        {"game.maps.paths", paths},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
