// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "battle_scenario.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

class CMatrixBuilding;
class CMatrixRobotAI;
struct SObjectCore;

namespace tests::production {
struct View {
    CMatrixBuilding *base;
    SObjectCore *base_core;
    CMatrixRobotAI *robot = nullptr;
    SObjectCore *robot_core = nullptr;
    int payouts = 0, paid = -1, spawned = -1, opened = -1, detached = -1, closed = -1;
    std::optional<int> capacity{};
    std::optional<std::array<int, 4>> constructor_price{};
    std::optional<std::array<std::array<int, 3>, 2>> weapon_samples{};
    std::optional<float> detach_distance{};
};

const char *ending_name(int ending);
std::string observe(const battle::Session &session, const View &view, const char *phase);
void invalidate(int ending);
void complete_after_cleanup();

class Recording {
public:
    Recording(const battle::Session &session, random::Mode mode, int step, int ending);
    void command(const char *name, std::uint64_t before);
    void snapshot(const std::string &payload);
    void finish();
private:
    const battle::Session &session_;
    std::filesystem::path path_;
    std::ofstream output_;
    std::uint64_t hash_ = 14695981039346656037ull;
    unsigned observations_{};
};
} // namespace tests::production
