// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "ScreenLayout.hpp"
#include "3g.hpp"

#include <utility>

namespace {

using ScreenLayout::Position;

constexpr std::pair<int, int> sizes[] = {{640, 480}, {1024, 768}, {1280, 720}, {1920, 1080}, {2560, 1440}, {8192, 8192}};

// Layout coordinates of the anchored battle panels in the tracked interface configuration.
constexpr int layout_x[] = {0, 340, 447};
constexpr int layout_y[] = {0, 204, 559, 564, 583};

void anchoring() {
    for (const auto &[width, height] : sizes) {
        MG_CHECK(ScreenLayout::anchor(0, ScreenLayout::layout_width, width) == 0);
        MG_CHECK(ScreenLayout::anchor(0, ScreenLayout::layout_height, height) == 0);
        for (int x : {340, 447})
            MG_CHECK(ScreenLayout::anchor(x, ScreenLayout::layout_width, width) == width - (1024 - x));
        for (int y : {204, 559, 564, 583})
            MG_CHECK(ScreenLayout::anchor(y, ScreenLayout::layout_height, height) == height - (768 - y));
    }
    // The authored size reproduces the authored layout.
    for (int x : layout_x) MG_CHECK(ScreenLayout::anchor(x, 1024, 1024) == x);
    for (int y : layout_y) MG_CHECK(ScreenLayout::anchor(y, 768, 768) == y);
    // Shifting an anchored panel by the change in screen size equals laying it out at the new size.
    for (const auto &[old_width, old_height] : sizes)
        for (const auto &[width, height] : sizes) {
            for (int x : layout_x) {
                const int shift = x == 0 ? 0 : width - old_width;
                MG_CHECK(ScreenLayout::anchor(x, 1024, old_width) + shift == ScreenLayout::anchor(x, 1024, width));
            }
            for (int y : layout_y) {
                const int shift = y == 0 ? 0 : height - old_height;
                MG_CHECK(ScreenLayout::anchor(y, 768, old_height) + shift == ScreenLayout::anchor(y, 768, height));
            }
        }
}

void dialog_centring() {
    MG_CHECK((ScreenLayout::centred_dialog(1024, 768, 300, 200) == Position{362, 284 - 69}));
    MG_CHECK((ScreenLayout::centred_dialog(640, 480, 300, 200) == Position{170, 140 - 43}));
    MG_CHECK((ScreenLayout::centred_dialog(1920, 1080, 301, 201) == Position{809, 439 - 97}));
    for (const auto &[old_width, old_height] : sizes)
        for (const auto &[width, height] : sizes) {
            const auto centred = ScreenLayout::centred_dialog(old_width, old_height, 301, 201);
            const auto moved = ScreenLayout::follow_screen(centred, 301, 201, old_width, old_height, width, height);
            MG_CHECK(moved == ScreenLayout::centred_dialog(width, height, 301, 201));
            MG_CHECK(ScreenLayout::follow_screen(moved, 301, 201, width, height, old_width, old_height) == centred);
            // Dialogs shown at fixed positions, such as the battle start text, stay in place.
            MG_CHECK((ScreenLayout::follow_screen({20, 62}, 301, 201, old_width, old_height, width, height) ==
                      Position{20, 62}));
        }
}

void layout_tracking() {
    const int width = g_ScreenX, height = g_ScreenY;
    ScreenLayout::laid_out(0, 0);
    g_ScreenX = 1920;
    g_ScreenY = 1080;
    MG_CHECK(!ScreenLayout::update());  // Nothing is laid out before the battle interface loads.
    ScreenLayout::laid_out(1920, 1080);
    MG_CHECK(!ScreenLayout::update());  // The current size needs no new layout.
    ScreenLayout::laid_out(0, 0);
    g_ScreenX = width;
    g_ScreenY = height;
}

constexpr tests::Case cases[] = {
    {"game.screen.anchoring", anchoring},
    {"game.screen.dialog_centring", dialog_centring},
    {"game.screen.tracking", layout_tracking},
};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
