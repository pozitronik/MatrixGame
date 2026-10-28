// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class CMatrixFlyer;
struct SObjectCore;

namespace tests::c2 {

// This recorder belongs to the four finite command-lifecycle cases only.
void invalidate(const char *family);
void complete_after_cleanup();
void tick(battle::Session &session);
void command(const char *name, std::uint64_t draws_before);
void observe(const char *phase);

class Recording {
public:
    Recording(battle::Session &session, const char *family, random::Mode mode, int step, int route, bool flag);
    ~Recording();
    Recording(const Recording &) = delete;
    Recording &operator=(const Recording &) = delete;

    void extra(CMatrixRobotAI *robot);
    void carrier(CMatrixFlyer *flyer) { carrier_ = flyer; }
    void core(SObjectCore *value) { core_ = value; }
    void state(const char *phase);
    void command(const char *name, std::uint64_t draws_before);
    void finish(int intermediate = -1, int ready = -1);

private:
    int id(const void *object) const;
    CMatrixRobotAI *present(unsigned index) const;
    battle::Session &session_;
    std::filesystem::path path_;
    std::ofstream file_;
    std::string snapshot_buffer_;
    std::vector<CMatrixRobotAI *> actors_;
    std::vector<std::size_t> uids_;
    CMatrixFlyer *carrier_ = nullptr;
    SObjectCore *core_ = nullptr;
};

} // namespace tests::c2
