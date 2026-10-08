// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "Text/Parser.hpp"
#include "stupid_logger.hpp"

#include <algorithm>

logger_type lgr{std::cerr};

namespace {
std::wstring visible_text(const std::vector<Text::Token> &tokens) {
    std::wstring result;
    for (const auto &token : tokens) result += token.text;
    return result;
}

void words_and_colors() {
    Text::Font font(nullptr);
    const auto tokens = Text::parse_tokens(L"plain <CoLoR=1,2,3>red blue</cOlOr> tail", font);
    MG_CHECK(visible_text(tokens) == L"plain red blue tail");
    int colored = 0;
    for (const auto &token : tokens) {
        if (token.text == L"red" || token.text == L"blue") {
            MG_CHECK(token.color == 0xff010203u);
            MG_CHECK(token.width == token.text.size() * 6);
            ++colored;
        }
        if (token.text == L"plain" || token.text == L"tail") MG_CHECK(token.color == 0);
    }
    MG_CHECK(colored == 2);
}

void inline_colors_and_unicode() {
    Text::Font font(nullptr);
    const auto tokens = Text::parse_tokens(L"before<color=255,0,64>\u0420\u043e\u0431\u043e\u0442</color>after", font);
    MG_CHECK(tokens.size() == 3);
    MG_CHECK(visible_text(tokens) == L"before\u0420\u043e\u0431\u043e\u0442after");
    MG_CHECK(tokens[1].color == 0xffff0040u);
    MG_CHECK(tokens[0].color == 0);
    MG_CHECK(tokens[2].color == 0);
}

void newline_variants() {
    Text::Font font(nullptr);
    for (auto text : {L"a\r\nb", L"a\rb", L"a\nb"}) {
        MG_CHECK(visible_text(Text::parse_tokens(text, font)) == L"a\r\nb");
    }
    for (auto text : {L"\r", L"\n", L"\r\n"}) {
        MG_CHECK(visible_text(Text::parse_tokens(text, font)) == L"\r\n");
    }
}

struct GuardedText {
    void *allocation = nullptr;
    std::wstring_view view;

    explicit GuardedText(std::wstring_view text) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        MG_CHECK(text.size() * sizeof(wchar_t) < info.dwPageSize);
        allocation = VirtualAlloc(nullptr, info.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        MG_CHECK(allocation != nullptr);
        if (!VirtualAlloc(allocation, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("Could not commit synthetic text page");
        }
        auto *end = reinterpret_cast<wchar_t *>(static_cast<char *>(allocation) + info.dwPageSize);
        auto *start = end - text.size();
        std::copy(text.begin(), text.end(), start);
        view = std::wstring_view(start, text.size());
    }
    ~GuardedText() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};

void bounded_color_input() {
    Text::Font font(nullptr);
    for (auto text : {L"<color=", L"<color=1,2", L"<color=1,2,3"}) {
        GuardedText guarded(text);
        const auto tokens = Text::parse_tokens(guarded.view, font);
        MG_CHECK(visible_text(tokens) == text);
    }
    GuardedText valid(L"<color=1,2,3>ok</color>");
    const auto tokens = Text::parse_tokens(valid.view, font);
    MG_CHECK(visible_text(tokens) == L"ok");
    MG_CHECK(tokens[0].color == 0xff010203u);
}

void invalid_color_channels() {
    Text::Font font(nullptr);
    for (auto text : {L"<color=256,2,3>x</color>", L"<color=-1,2,3>x</color>",
                     L"<color=1,2,999>x</color>", L"<color=1,2,3junk>x</color>",
                     L"<color=1,2>x</color>", L"<color=1,2,3,4>x</color>"}) {
        const auto tokens = Text::parse_tokens(text, font);
        MG_CHECK(visible_text(tokens) == L"x");
        MG_CHECK(tokens[0].color == 0);
    }
}

void many_inline_tags() {
    Text::Font font(nullptr);
    std::wstring text;
    constexpr size_t count = 10000;
    for (size_t index = 0; index < count; ++index) text += L"<color=1,2,3>x</color>";
    const auto tokens = Text::parse_tokens(text, font);
    MG_CHECK(tokens.size() == count);
    MG_CHECK(visible_text(tokens) == std::wstring(count, L'x'));
    MG_CHECK(std::all_of(tokens.begin(), tokens.end(), [](const auto &token) {
        return token.text == L"x" && token.color == 0xff010203u && token.width == 6;
    }));
}

void line_layout() {
    Text::Font font(nullptr);
    MG_CHECK(Text::calc_lines(Text::parse_tokens(L"ab", font), font, RECT{0, 0, 12, 30}) == 1);
    MG_CHECK(Text::calc_lines(Text::parse_tokens(L"abc", font), font, RECT{0, 0, 12, 30}) == 1);
    MG_CHECK(Text::calc_lines(Text::parse_tokens(L"aa bb cc", font), font, RECT{0, 0, 18, 30}) == 3);
    MG_CHECK(Text::calc_lines(Text::parse_tokens(L"ab\r\ncd", font), font, RECT{0, 0, 12, 30}) == 2);
}

constexpr tests::Case cases[] = {
    {"game.text.words_colors", words_and_colors},
    {"game.text.inline_unicode", inline_colors_and_unicode},
    {"game.text.newlines", newline_variants},
    {"game.text.bounded_color", bounded_color_input},
    {"game.text.invalid_color", invalid_color_channels},
    {"game.text.many_tags", many_inline_tags},
    {"game.text.line_layout", line_layout},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    return tests::run(argc, argv, cases);
}
