// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

class CMatrixMapLogic;
class CMatrixRobotAI;
enum ERobotUnitKind : unsigned int;

namespace tests {

enum class NavigationLayout { CentralObstacle, Open, Corridor, DeadEnd, Disconnected };

// Initial fixture data only. Commands, navigation and ticks remain production code.
class BattleGeometry {
public:
    static constexpr int movement_size = 64;
    static constexpr int obstacle_left = 28;
    static constexpr int obstacle_right = 36;
    static constexpr int obstacle_top = 20;
    static constexpr int obstacle_bottom = 44;

    // Requires the active map/heap/config aliases. All allocations become map-owned.
    static void configure(CMatrixMapLogic &map, NavigationLayout layout = NavigationLayout::CentralObstacle);
    // Coordinates are world units. Returned units belong to map.StaticClear().
    static CMatrixRobotAI *create_robot(CMatrixMapLogic &map, int side, float x, float y);
    static CMatrixRobotAI *create_robot(CMatrixMapLogic &map, int side, float x, float y, ERobotUnitKind chassis);
};

}
