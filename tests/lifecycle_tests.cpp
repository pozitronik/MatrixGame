// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "Helper.hpp"
#include "Form.hpp"

#include <memory>
#include <vector>

namespace {
struct Services {
    Services() {
        CacheInit();
        g_Config.SetDefaults();
    }

    ~Services() {
        CGame::Deinit();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
    }
};

void populate_cursors(CMatrixConfig &config) {
    constexpr int count = 2;
    auto *pairs = static_cast<SStringPair *>(HAlloc(sizeof(SStringPair) * count, g_CacheHeap));
    int constructed = 0;
    try {
        for (; constructed < count; ++constructed) {
            std::construct_at(pairs + constructed, std::wstring(80, L'k'), std::wstring(160, L'v'));
        }
    }
    catch (...) {
        std::destroy_n(pairs, constructed);
        HFree(pairs, g_CacheHeap);
        throw;
    }
    config.m_Cursors = pairs;
    config.m_CursorsCnt = count;
}

void configuration_cleanup() {
    Services services;
    CMatrixConfig config;
    config.SetDefaults();
    for (int cycle = 0; cycle < 3; ++cycle) {
        populate_cursors(config);
        config.Clear();
        MG_CHECK(config.m_Cursors == nullptr);
        MG_CHECK(config.m_CursorsCnt == 0);
        config.Clear();
        MG_CHECK(config.m_Cursors == nullptr);
        MG_CHECK(config.m_CursorsCnt == 0);
    }
}

void empty_configuration_cleanup() {
    Services services;
    CMatrixConfig config;
    config.m_CursorsCnt = 17;
    config.SetDefaults();
    MG_CHECK(config.m_Cursors == nullptr);
    MG_CHECK(config.m_CursorsCnt == 0);
    config.Clear();
    config.Clear();
    MG_CHECK(config.m_CursorsCnt == 0);
}

struct RecordingForm : CForm {
    std::vector<int> &events;
    int id;

    RecordingForm(std::vector<int> &record, int identifier, bool fail = false) : events(record), id(identifier) {
        if (fail) {
            throw std::runtime_error("Synthetic form construction failure");
        }
    }

    ~RecordingForm() {
        if (g_FormCur == this) {
            FormChange(nullptr);
        }
    }

    void Enter() override { events.push_back(id * 10 + 1); }
    void Leave() override { events.push_back(id * 10 + 2); }
    void Draw() override { throw std::logic_error("Unexpected rendering in lifecycle fixture"); }
    void Takt(int) override {}
    void MouseMove(int, int) override {}
    void MouseKey(ButtonStatus, int, int, int) override {}
    void Keyboard(bool, uint8_t) override {}
    void SystemEvent(ESysEvent) override {}
};

struct FormSession {
    FormSession() { CForm::StaticInit(); }
    ~FormSession() { FormChange(nullptr); }
};

void form_transitions() {
    FormSession session;
    std::vector<int> events;
    {
        RecordingForm first(events, 1);
        RecordingForm second(events, 2);
        MG_CHECK(g_FormFirst == &first);
        MG_CHECK(g_FormLast == &second);
        FormChange(&first);
        FormChange(&second);
        FormChange(nullptr);
        MG_CHECK(g_FormCur == nullptr);
        MG_CHECK((events == std::vector<int>{11, 12, 21, 22}));
    }
    MG_CHECK(g_FormFirst == nullptr);
    MG_CHECK(g_FormLast == nullptr);
}

void form_construction_failure() {
    FormSession session;
    std::vector<int> events;
    RecordingForm survivor(events, 1);
    bool caught = false;
    try {
        RecordingForm failed(events, 2, true);
    }
    catch (const std::runtime_error &) {
        caught = true;
    }
    MG_CHECK(caught);
    MG_CHECK(g_FormFirst == &survivor);
    MG_CHECK(g_FormLast == &survivor);
    MG_CHECK(g_FormCur == nullptr);
    MG_CHECK(events.empty());
}

void repeated_form_sessions() {
    FormSession session;
    std::vector<int> events;
    for (int cycle = 0; cycle < 3; ++cycle) {
        {
            RecordingForm form(events, cycle + 1);
            FormChange(&form);
            MG_CHECK(g_FormCur == &form);
            FormChange(nullptr);
        }
        MG_CHECK(g_FormCur == nullptr);
        MG_CHECK(g_FormFirst == nullptr);
        MG_CHECK(g_FormLast == nullptr);
    }
    MG_CHECK((events == std::vector<int>{11, 12, 21, 22, 31, 32}));
}

void check_empty_game() {
    MG_CHECK(g_MatrixHeap == nullptr);
    MG_CHECK(g_MatrixData == nullptr);
    MG_CHECK(g_MatrixMap == nullptr);
    MG_CHECK(g_Render == nullptr);
    MG_CHECK(g_IFaceList == nullptr);
    MG_CHECK(g_ConfigHistory == nullptr);
    MG_CHECK(g_Config.m_Cursors == nullptr);
    MG_CHECK(g_Config.m_CursorsCnt == 0);
    MG_CHECK(g_D3DD == nullptr);
}

void partial_startup_cleanup() {
    for (int stage = 0; stage < 5; ++stage) {
        Services services;
        if (stage >= 1) {
            g_MatrixHeap = HNew(nullptr) CHeap;
        }
        if (stage >= 2) {
            g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
            g_MatrixData->ParAdd(L"Synthetic", L"Partial startup");
        }
        if (stage >= 3) {
            populate_cursors(g_Config);
        }
        if (stage >= 4) {
            g_MatrixMap = HNew(g_MatrixHeap) CMatrixMapLogic;
        }
        CGame::Deinit();
        check_empty_game();
        CGame::Deinit();
        check_empty_game();
        CacheDeinit();
        CacheDeinit();
        MG_CHECK(g_Cache == nullptr);
        MG_CHECK(g_CacheHeap == nullptr);
    }
}

constexpr tests::Case cases[] = {
    {"game.lifecycle.configuration_cleanup", configuration_cleanup},
    {"game.lifecycle.empty_configuration", empty_configuration_cleanup},
    {"game.lifecycle.form_transitions", form_transitions},
    {"game.lifecycle.form_construction_failure", form_construction_failure},
    {"game.lifecycle.repeated_forms", repeated_form_sessions},
    {"game.lifecycle.partial_startup", partial_startup_cleanup},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Lifecycle test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
