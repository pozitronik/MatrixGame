// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "RandomConfiguration.hpp"
#include "CBlockPar.hpp"
#include "stupid_logger.hpp"

#include <filesystem>
#include <sstream>

logger_type lgr{std::cerr};

namespace {

void default_mode() {
    Base::CBlockPar options;
    random::seed(1, random::Mode::LegacyCRT);
    const auto selection = RandomConfiguration::initialize(1, options);
    MG_CHECK(selection.valid && selection.mode == random::Mode::ParkMiller);
    MG_CHECK(random::Rnd() == 16806);
}

void explicit_mode() {
    for (const auto *value : {L"LegacyCRT", L"  legacycrt\t"}) {
        Base::CBlockPar options;
        options.ParAdd(L"RandomGenerator", value);
        const auto selection = RandomConfiguration::initialize(1, options);
        MG_CHECK(selection.valid && selection.mode == random::Mode::LegacyCRT);
        MG_CHECK(random::Rnd() == 41);
    }
    Base::CBlockPar options;
    options.ParAdd(L"RandomGenerator", L"ParkMiller");
    MG_CHECK(RandomConfiguration::initialize(1, options).valid);
    MG_CHECK(random::mode() == random::Mode::ParkMiller && random::Rnd() == 16806);
}

void invalid_mode() {
    for (const auto *value : {L"", L"unknown", L"LegacyCRT trailing"}) {
        Base::CBlockPar options;
        options.ParAdd(L"RandomGenerator", value);
        const auto selection = RandomConfiguration::initialize(1, options);
        MG_CHECK(!selection.valid && selection.mode == random::Mode::ParkMiller);
        MG_CHECK(random::Rnd() == 16806);
    }
    Base::CBlockPar options;
    options.ParAdd(L"RandomGenerator", L"ParkMiller");
    options.ParAdd(L"RandomGenerator", L"LegacyCRT");
    MG_CHECK(!RandomConfiguration::initialize(1, options).valid);
    MG_CHECK(random::mode() == random::Mode::ParkMiller && random::Rnd() == 16806);
}

void fixed_battle_mode() {
    Base::CBlockPar options;
    options.ParAdd(L"RandomGenerator", L"LegacyCRT");
    MG_CHECK(RandomConfiguration::initialize(1, options).valid);
    MG_CHECK(random::Rnd() == 41);
    options.ParSetAdd(L"RandomGenerator", L"ParkMiller");
    MG_CHECK(RandomConfiguration::resolve(options).mode == random::Mode::ParkMiller);
    MG_CHECK(random::mode() == random::Mode::LegacyCRT && random::Rnd() == 18467);
    MG_CHECK(RandomConfiguration::initialize(1, options).valid);
    MG_CHECK(random::mode() == random::Mode::ParkMiller && random::Rnd() == 16806);
}

struct OptionsFile {
    std::filesystem::path path = std::filesystem::current_path() /
        ("matrixgame-rng-options-" + std::to_string(GetCurrentProcessId()) + ".txt");
    OptionsFile() { MG_CHECK(!std::filesystem::exists(path)); }
    ~OptionsFile() { std::error_code error; std::filesystem::remove(path, error); }
    const wchar_t *name() const { return path.c_str(); }
    void write(const Base::CBuf &bytes) { bytes.SaveInFile(path.native()); }
};

void missing_file() {
    OptionsFile file;
    random::seed(1, random::Mode::LegacyCRT);
    const auto selection = RandomConfiguration::initialize_standalone(1, file.name());
    MG_CHECK(selection.valid && selection.mode == random::Mode::ParkMiller);
    MG_CHECK(random::Rnd() == 16806);
}

void configuration_file() {
    OptionsFile file;
    Base::CBuf bytes;
    bytes.StrNZ("RandomGenerator=LegacyCRT\n");
    file.write(bytes);
    MG_CHECK(RandomConfiguration::initialize_standalone(1, file.name()).valid);
    MG_CHECK(random::mode() == random::Mode::LegacyCRT && random::Rnd() == 41);
    bytes.Clear();
    bytes.Add<uint16_t>(0xfeff);
    bytes.WStrNZ(L"RandomGenerator=LegacyCRT\r\n");
    file.write(bytes);
    MG_CHECK(RandomConfiguration::initialize_standalone(1, file.name()).valid);
    MG_CHECK(random::mode() == random::Mode::LegacyCRT && random::Rnd() == 41);
    bytes.Clear();
    bytes.Add<uint8_t>(0xc3);
    bytes.Add<uint8_t>(0x28);
    file.write(bytes);
    const auto rejected = RandomConfiguration::initialize_standalone(1, file.name());
    MG_CHECK(!rejected.valid && rejected.mode == random::Mode::ParkMiller);
    MG_CHECK(random::Rnd() == 16806);
}

void configuration_report() {
    std::ostringstream output;
    logger::stupid<logger::level::warning> release_log(output);
    release_log.info("ordinary info");
    MG_CHECK(output.str().empty());
    release_log.notice("Battle random generator: {}")(RandomConfiguration::name(random::Mode::LegacyCRT));
    MG_CHECK(output.str().find(" INFO| Battle random generator: LegacyCRT") != std::string::npos);
    MG_CHECK(output.str().find("ordinary info") == std::string::npos);
}

constexpr tests::Case cases[] = {
    {"game.random.default_mode", default_mode}, {"game.random.explicit_mode", explicit_mode},
    {"game.random.invalid_mode", invalid_mode}, {"game.random.fixed_battle_mode", fixed_battle_mode},
    {"game.random.missing_file", missing_file}, {"game.random.configuration_file", configuration_file},
    {"game.random.configuration_report", configuration_report},
};

} // namespace

int main(int argc, char **argv) {
    Base::CMain::BaseInit();
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
