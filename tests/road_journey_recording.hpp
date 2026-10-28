// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class CMatrixRobotAI;
class CMatrixMapStatic;
struct SObjectCore;
struct SMatrixPlayerGroup;

namespace tests::road {
struct Metrics {
    std::array<unsigned, 2> zones{};
    std::array<int, 2> crossed{}, arrived{};
    float minimum_separation = 10000;
    int retired{};
};

void invalidate(random::Mode mode, int step, bool obstruction);
void complete_after_cleanup();

class Recording {
public:
    Recording(const battle::Session &session, random::Mode mode, int step, const char *variant, int replay);
    void command(const char *name, std::uint64_t before, std::array<std::uint32_t, 3> point = {});
    void snapshot(const std::string &original, const SMatrixPlayerGroup &group, SObjectCore *core,
                  CMatrixRobotAI *blocker, const Metrics &metrics, bool source_observation = true);
    void finish(const std::vector<std::string> &trace);

private:
    const battle::Session &session_;
    std::filesystem::path path_;
    std::ofstream output_;
};
} // namespace tests::road
