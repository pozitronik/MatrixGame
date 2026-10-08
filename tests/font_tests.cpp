// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "Text/FontCache.hpp"
#include "CException.hpp"
#include "stupid_logger.hpp"
#include "utils.hpp"

#include <array>
#include <memory>
#include <vector>

logger_type lgr{std::cerr};

namespace tests { void native_font_reset(); }

namespace {
struct SyntheticFont : ID3DXFont {
    int released = 0;
    int lost = 0;
    int reset = 0;
    HRESULT lostResult = S_OK;
    HRESULT resetResult = S_OK;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { throw std::logic_error("Unexpected font AddRef"); }
    ULONG STDMETHODCALLTYPE Release() override { ++released; return 0; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDescA(D3DXFONT_DESCA *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDescW(D3DXFONT_DESCW *) override { return E_NOTIMPL; }
    BOOL STDMETHODCALLTYPE GetTextMetricsA(TEXTMETRICA *) override { return FALSE; }
    BOOL STDMETHODCALLTYPE GetTextMetricsW(TEXTMETRICW *) override { return FALSE; }
    HDC STDMETHODCALLTYPE GetDC() override { return nullptr; }
    HRESULT STDMETHODCALLTYPE GetGlyphData(UINT, IDirect3DTexture9 **, RECT *, POINT *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreloadCharacters(UINT, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreloadGlyphs(UINT, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreloadTextA(const char *, INT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreloadTextW(const wchar_t *, INT) override { return E_NOTIMPL; }
    INT STDMETHODCALLTYPE DrawTextA(ID3DXSprite *, const char *, INT, RECT *, DWORD, D3DCOLOR) override { return 0; }
    INT STDMETHODCALLTYPE DrawTextW(ID3DXSprite *, const wchar_t *, INT, RECT *, DWORD, D3DCOLOR) override { return 0; }
    HRESULT STDMETHODCALLTYPE OnLostDevice() override { ++lost; return lostResult; }
    HRESULT STDMETHODCALLTYPE OnResetDevice() override { ++reset; return resetResult; }
};

struct LoadRequest {
    std::wstring face;
    size_t size;
};

struct FontFixture {
    static FontFixture *current;
    std::array<SyntheticFont, 5> fonts;
    std::vector<LoadRequest> requests;
    int missing = -1;
    int failAt = -1;

    FontFixture() { current = this; }
    ~FontFixture() { current = nullptr; }

    static LPD3DXFONT load(IDirect3DDevice9 *device, std::wstring_view face, size_t size) {
        MG_CHECK(device == nullptr);
        MG_CHECK(current != nullptr);
        const auto index = current->requests.size();
        MG_CHECK(index < current->fonts.size());
        current->requests.push_back({std::wstring(face), size});
        if (static_cast<int>(index) == current->failAt) {
            throw std::runtime_error("Synthetic font creation failure");
        }
        return static_cast<int>(index) == current->missing ? nullptr : &current->fonts[index];
    }
};
FontFixture *FontFixture::current = nullptr;

void cache_identity_and_ownership() {
    FontFixture fixture;
    {
        Text::FontCache cache(nullptr, FontFixture::load);
        constexpr std::array names{L"Font.1Normal", L"Font.2Mini", L"Font.2Small", L"Font.2Normal", L"Font.2Ranger"};
        constexpr std::array<size_t, 5> sizes{13, 12, 13, 14, 10};
        MG_CHECK(fixture.requests.size() == names.size());
        for (size_t index = 0; index < names.size(); ++index) {
            MG_CHECK(fixture.requests[index].size == sizes[index]);
            MG_CHECK(fixture.requests[index].face == (index == 4 ? L"Rangers" : L"Verdana"));
            MG_CHECK(cache.Get(names[index]).operator->() == &fixture.fonts[index]);
            MG_CHECK(&cache.Get(names[index]) == &cache.Get(names[index]));
        }
        bool caught = false;
        try { cache.Get(L"Font.Unknown"); }
        catch (const std::runtime_error &error) {
            caught = std::string_view(error.what()).find("Font.Unknown") != std::string_view::npos;
        }
        MG_CHECK(caught);
        MG_CHECK(fixture.requests.size() == 5);
    }
    for (const auto &font : fixture.fonts) {
        MG_CHECK(font.released == 1);
    }
}

void repeated_device_notifications() {
    // Unused UI fonts must stay uninitialized when a map receives notifications.
    Text::OnLostDevice();
    Text::OnResetDevice();
    FontFixture fixture;
    Text::FontCache cache(nullptr, FontFixture::load);
    for (int cycle = 1; cycle <= 3; ++cycle) {
        cache.OnLostDevice();
        for (const auto &font : fixture.fonts) {
            MG_CHECK(font.lost == cycle);
            MG_CHECK(font.reset == cycle - 1);
            MG_CHECK(font.released == 0);
        }
        cache.OnResetDevice();
        for (const auto &font : fixture.fonts) {
            MG_CHECK(font.reset == cycle);
        }
    }
    MG_CHECK(fixture.requests.size() == 5);
}

void missing_font_notifications() {
    FontFixture fixture;
    fixture.missing = 2;
    {
        Text::FontCache cache(nullptr, FontFixture::load);
        MG_CHECK(!cache.Get(L"Font.2Small"));
        cache.OnLostDevice();
        cache.OnResetDevice();
    }
    for (size_t index = 0; index < fixture.fonts.size(); ++index) {
        const int expected = index == 2 ? 0 : 1;
        MG_CHECK(fixture.fonts[index].lost == expected);
        MG_CHECK(fixture.fonts[index].reset == expected);
        MG_CHECK(fixture.fonts[index].released == expected);
    }
}

void partial_font_creation_cleanup() {
    FontFixture fixture;
    fixture.failAt = 2;
    bool caught = false;
    try { Text::FontCache cache(nullptr, FontFixture::load); }
    catch (const std::runtime_error &) { caught = true; }
    MG_CHECK(caught);
    MG_CHECK(fixture.requests.size() == 3);
    for (size_t index = 0; index < fixture.fonts.size(); ++index) {
        MG_CHECK(fixture.fonts[index].released == (index < 2 ? 1 : 0));
    }
}

void notification_failure_diagnostics() {
    FontFixture fixture;
    Text::FontCache cache(nullptr, FontFixture::load);
    fixture.fonts[2].lostResult = D3DERR_INVALIDCALL;
    bool caught = false;
    try { cache.OnLostDevice(); }
    catch (const std::runtime_error &error) {
        const std::string_view message(error.what());
        caught = message.find("OnLostDevice") != message.npos && message.find("Font.2Small") != message.npos &&
                 message.find("8876086C") != message.npos;
    }
    MG_CHECK(caught);
    fixture.fonts[2].lostResult = S_OK;
    fixture.fonts[2].resetResult = D3DERR_INVALIDCALL;
    caught = false;
    try { cache.OnResetDevice(); }
    catch (const std::runtime_error &error) {
        const std::string_view message(error.what());
        caught = message.find("OnResetDevice") != message.npos && message.find("Font.2Small") != message.npos;
    }
    MG_CHECK(caught);
}

void native_reset_with_diagnostic() {
    try { tests::native_font_reset(); }
    catch (const Base::CException &error) {
        throw std::runtime_error(utils::from_wstring(error.Info()));
    }
}

constexpr tests::Case cases[] = {
    {"game.font.cache_ownership", cache_identity_and_ownership},
    {"game.font.device_notifications", repeated_device_notifications},
    {"game.font.missing_font", missing_font_notifications},
    {"game.font.partial_creation", partial_font_creation_cleanup},
    {"game.font.failure_diagnostics", notification_failure_diagnostics},
    {"manual.font.native_reset", native_reset_with_diagnostic},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    return tests::run(argc, argv, cases);
}
