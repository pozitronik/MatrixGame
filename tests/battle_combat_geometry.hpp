// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <memory>
class CMatrixCannon;
class CMatrixRobotAI;
enum ERobotUnitKind : unsigned int;
namespace tests {
// Construct before Session and install after it: Session retires effects before
// this guard restores borrowed static catalogs. Native draw/upload is excluded.
class BattleCombatAssets {
public:
    BattleCombatAssets();
    ~BattleCombatAssets();
    void install();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Requires an active BattleGeometry map. Assets are synthetic cache data; the
// cannon's graph, weapon ownership, attachment transforms and ticks are production.
CMatrixCannon *create_battle_cannon(int side, float x, float y);
void arm_battle_robot(CMatrixRobotAI &robot, ERobotUnitKind weapon);
}
