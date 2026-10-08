// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "input.hpp"
#include "MatrixConfig.hpp"

CMatrixConfig g_Config;

namespace {

void key_state() {
    Input::onFocusLost();
    Input::onKeyDown(VK_A);
    Input::onKeyDown(VK_D);
    Input::onKeyDown(VK_A);
    MG_CHECK(Input::isVKeyPressed(VK_A));
    MG_CHECK(Input::isVKeyPressed(VK_D));
    MG_CHECK(!Input::isVKeyPressed(VK_W));
    Input::onKeyUp(VK_A);
    Input::onKeyUp(VK_A);
    MG_CHECK(!Input::isVKeyPressed(VK_A));
    MG_CHECK(Input::isVKeyPressed(VK_D));
    Input::onKeyUp(VK_D);
    MG_CHECK(!Input::isVKeyPressed(VK_D));
}

void focus_reset() {
    for (unsigned key = 0; key < 256; ++key) Input::onKeyDown(static_cast<uint8_t>(key));
    for (unsigned key = 0; key < 256; ++key) MG_CHECK(Input::isVKeyPressed(static_cast<uint8_t>(key)));
    // No key-up notifications arrive while another window owns keyboard input.
    Input::onFocusLost();
    Input::onFocusLost();
    for (unsigned key = 0; key < 256; ++key) MG_CHECK(!Input::isVKeyPressed(static_cast<uint8_t>(key)));
    Input::onKeyDown(VK_W);
    MG_CHECK(Input::isVKeyPressed(VK_W));
    MG_CHECK(!Input::isVKeyPressed(VK_CONTROL));
    Input::onKeyUp(VK_W);
    MG_CHECK(!Input::isVKeyPressed(VK_W));
}

void action_bindings() {
    Input::onFocusLost();
    g_Config.m_KeyActions[KA_SCROLL_LEFT] = VK_A;
    g_Config.m_KeyActions[KA_SCROLL_RIGHT] = VK_D;
    Input::onKeyDown(VK_A);
    MG_CHECK(Input::isKeyPressed(KA_SCROLL_LEFT));
    MG_CHECK(!Input::isKeyPressed(KA_SCROLL_RIGHT));
    Input::onFocusLost();
    MG_CHECK(!Input::isKeyPressed(KA_SCROLL_LEFT));
    Input::onKeyDown(VK_D);
    MG_CHECK(!Input::isKeyPressed(KA_SCROLL_LEFT));
    MG_CHECK(Input::isKeyPressed(KA_SCROLL_RIGHT));
}

constexpr tests::Case cases[] = {
    {"game.input.key_state", key_state},
    {"game.input.focus_reset", focus_reset},
    {"game.input.action_bindings", action_bindings},
};
}

int main(int argc, char **argv) {
    return tests::run(argc, argv, cases);
}
