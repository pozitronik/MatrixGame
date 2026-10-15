// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "DelayedDamage.hpp"
#include "GroupCounts.hpp"
#include "PlaceContainment.hpp"
#include "ZeroXTrace.hpp"
#include "scenario_build_identity.hpp"
#include "battle_device.hpp"
#include "battle_geometry.hpp"
#include "command_world.hpp"
#include "audio_fixture.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixWater.hpp"
#include "MatrixSoundManager.hpp"
#include "MatrixSkinManager.hpp"
#include "SoundBridge.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceMenu.h"
#include "input.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include <bit>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <float.h>

namespace tests::battle {

// Synthetic initial effect data; all landscape construction/ticks stay production.
struct EffectSetup : CMatrixEffect {
    static SSpotProperties install() {
        auto &p = m_SpotProperties[SPOT_SOLE_WHEEL];
        const auto previous = p;
        p.color = 0xffffffff;
        p.func = SpotTaktConstant;
        p.texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-wheel"));
        p.ttl = 30000;
        p.flags = 0;
        return previous;
    }
    static void restore(SSpotProperties previous) { m_SpotProperties[SPOT_SOLE_WHEEL] = previous; }
};

// Initial clock input only; all subsequent time advances use the production map tick.
struct ScenarioMap : CMatrixMapLogic {
    void initial_time(int value) { m_Time = value; }
};

struct Session::Impl {
    Profile profile;
    CHeap heap;
    CBlockPar data;
    BattleDevice device;
    EnginePtr<ScenarioMap> map;
    EnginePtr<CIFaceList> ui;
    std::vector<CMatrixRobotAI *> entities;
    std::vector<size_t> entity_uids;
    std::shared_ptr<audio_fixture::DeviceState> audio_state = std::make_shared<audio_fixture::DeviceState>();
    CMatrixConfig previous_config = g_Config;
    DWORD previous_flags = g_Flags;
    int previous_width = g_ScreenX;
    int previous_height = g_ScreenY;
    int previous_max_fps = g_MaxFPS;
    unsigned previous_precision = _controlfp(0, 0);
    int frames = 0;
    bool cache = false;
    bool effects = false;
    SSpotProperties previous_spot{};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    explicit Impl(Profile p) : profile(p), device(p.unavailable_icon_targets ?
        BattleDevice::IconTargets::Unavailable : BattleDevice::IconTargets::Forbidden) {
        MG_CHECK(g_MatrixMap == nullptr && g_MatrixHeap == nullptr && g_MatrixData == nullptr);
        MG_CHECK(g_IFaceList == nullptr && g_Cache == nullptr && !SoundBridge::available());
        MG_CHECK(g_PopupMenu == nullptr);
        MG_CHECK(g_RangersInterface == nullptr);
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
        MG_CHECK(p.step_ms == 10 || p.step_ms == 16);
        MG_CHECK(p.max_frames > 0 && p.max_frames <= 6000);
        MG_CHECK(p.initial_time_ms >= 0 && p.initial_time_ms <= 60000);
    }

    void setup(SetupFailure failure) {
#if defined(__MINGW32__)
        _controlfp(_PC_24, _MCW_PC); // Match the supported x86 D3D device profile.
#endif
        random::seed(profile.seed, profile.mode);
        g_Flags = 0;
        g_ScreenX = 1280;
        g_ScreenY = 720;
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        g_Config.SetDefaults();
        g_Config.m_ItemChars[CHASSIS2_MOVE_SPEED] = .6f;
        g_Config.m_ItemChars[CHASSIS2_ROTATION_SPEED] = .04f;
        g_Config.m_ItemChars[CHASSIS2_STRUCTURE] = 1000;
        g_Config.m_ItemChars[CHASSIS2_MOVE_WATER_CORR] = .5f;
        g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_DOWN] = 2.5f;
        g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_UP] = .7f;
        Input::onFocusLost();
        CacheInit();
        cache = true;
        previous_spot = EffectSetup::install();
        effects = true;
        data.BlockGetAdd(L"Sounds")->BlockGetAdd(L"dummy")->ParSetAdd(L"path", L"Sound.Synthetic");
        data.BlockGetAdd(L"Chars")->BlockGetAdd(L"Heads");
        data.BlockGetAdd(L"Chars")->BlockGetAdd(L"ChassisSounds")->BlockGetAdd(L"2")->ParSetAdd(L"MoveTo", L"Sound.Synthetic");
        if (profile.audio) {
            SoundBridge::install(std::make_unique<Audio::Service>(
                std::make_unique<audio_fixture::FakeDevice>(audio_state),
                Audio::Mapping{{L"Sound.Synthetic", L"synthetic.wav"}},
                [](const std::wstring &) { return audio_fixture::wave(); },
                [](const std::wstring &, const std::string &error) { throw std::runtime_error(error); }));
        }
        map.reset(HNew(g_MatrixHeap) ScenarioMap);
        map->initial_time(profile.initial_time_ms);
        g_MatrixMap = map.get();
        ui.reset(HNew(g_MatrixHeap) CIFaceList);
        g_IFaceList = ui.get();
        map->m_Ids = static_cast<std::wstring *>(HAlloc(sizeof(std::wstring), g_MatrixHeap));
        new(map->m_Ids) std::wstring(L"synthetic-battle-v1");
        map->m_IdsCnt = 1;
        map->m_Side = static_cast<CMatrixSideUnit *>(HAllocClear(2 * sizeof(CMatrixSideUnit), g_MatrixHeap));
        for (int i = 0; i < 2; ++i) {
            new(map->m_Side + i) CMatrixSideUnit;
            ++map->m_SideCnt;
            map->m_Side[i].m_Id = i + 1;
            map->m_Side[i].SetStatus(SS_ACTIVE);
        }
        map->m_PlayerSide = map->m_Side;
        side().InitPlayerSide();
        BattleGeometry::configure(*map, profile.layout);
        map->m_Camera.SetXYStrategy(D3DXVECTOR2(320, 320));
        map->m_Camera.Takt(0);
        map->m_Camera.BeforeDraw();
        map->m_Cursor.SetPos(640, 360);
        if (failure == SetupFailure::Topology) throw std::runtime_error("injected topology setup failure");
        map->m_Water = HNew(g_MatrixHeap) CMatrixWater;
        // The no-draw fixture supplies the real water simulation's upload target.
        // Init additionally loads textures; those have no use in this profile.
        CREATE_VB_DYNAMIC((WATER_SIZE + 1) * (WATER_SIZE + 1) * sizeof(SMatrixMapWaterVertex),
                          MATRIX_WATER_VERTEX_FORMAT, map->m_Water->m_VB);
        map->m_Water->FillVB(0);
        entities.push_back(BattleGeometry::create_robot(*map, PLAYER_SIDE, 100, 200));
        entities.push_back(BattleGeometry::create_robot(*map, PLAYER_SIDE, 100, 260));
        entities.push_back(BattleGeometry::create_robot(*map, 2, 500, 500));
        entities.push_back(BattleGeometry::create_robot(*map, 2, 540, 540));
        entities[2]->SetTeam(0);
        entities[3]->SetTeam(0);
        for (auto *entity : entities) entity_uids.push_back(entity->UID);
        if (failure == SetupFailure::Entities) throw std::runtime_error("injected entity setup failure");
        if (profile.initially_selected) select({1, 2});
        if (failure == SetupFailure::Selection) throw std::runtime_error("injected selection setup failure");
    }

    ~Impl() {
        Input::onFocusLost();
        if (map && map->m_PlayerSide) {
            if (side().GetCurSelGroup()) side().GetCurSelGroup()->RemoveAll();
            side().SetCurGroup(nullptr);
        }
        // Release robots while their map, sides and interface still exist.
        if (map) map->StaticClear();
        if (map) map->LeaveDialogMode();
        ui.reset();
        g_IFaceList = nullptr;
        map.reset();
        // The map destructor frees skins but does not reset their static slots.
        CSkinManager::StaticInit();
        g_MatrixMap = nullptr;
        CSound::Clear();
        SoundBridge::shutdown();
        if (effects) EffectSetup::restore(previous_spot);
#if defined(_DEBUG) && !defined(_RELDEBUG)
        CHelper::ClearAll();
#endif
        if (cache) CacheDeinit();
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
        g_Config = previous_config;
        g_Flags = previous_flags;
        g_ScreenX = previous_width;
        g_ScreenY = previous_height;
        g_MaxFPS = previous_max_fps;
        _controlfp(previous_precision, _MCW_PC);
    }

    CMatrixSideUnit &side() const { return *map->GetPlayerSide(); }
    CMatrixRobotAI *lookup(EntityId id) const {
        if (!id || id > entities.size()) return nullptr;
        auto *candidate = entities[id - 1];
        for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic()) {
            if (live == candidate && live->IsRobot())
                return live->AsRobot()->UID == entity_uids[id - 1] ? live->AsRobot() : nullptr;
        }
        return nullptr;
    }
    int id_of(const CMatrixMapStatic *object) const {
        if (!object) return 0;
        for (EntityId id = 1; id <= entities.size(); ++id) if (lookup(id) == object) return static_cast<int>(id);
        return -1;
    }
    void select(const std::vector<EntityId> &ids) {
        for (const auto id : ids) MG_CHECK(lookup(id) && lookup(id)->m_Side == PLAYER_SIDE);
        auto *group = side().GetCurSelGroup();
        group->RemoveAll();
        for (auto id : ids) group->AddObject(lookup(id), -4);
        side().SetCurGroup(group);
        side().m_CurrSel = ids.empty() ? NOTHING_SELECTED : ids.size() == 1 ? ROBOT_SELECTED : GROUP_SELECTED;
    }
};

Session::Session(Profile p, SetupFailure failure) : impl_(std::make_unique<Impl>(p)) { impl_->setup(failure); }
Session::~Session() = default;
void Session::ground_command(float x, float y) {
    auto &s = *impl_;
    s.map->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    s.map->m_TraceStopPos = D3DXVECTOR3(x, y, 0);
    s.side().OnRButtonDown(CPoint(0, 0));
}
bool Session::target_command(EntityId id) {
    auto &s = *impl_;
    auto *target = s.lookup(id);
    if (!target) return false; // Retired scenario IDs never become dangling engine pointers.
    s.map->m_TraceStopObj = target;
    s.map->m_TraceStopPos = D3DXVECTOR3(target->m_PosX, target->m_PosY, 0);
    s.side().OnRButtonDown(CPoint(0, 0));
    return true;
}
bool Session::remove(EntityId id) {
    auto &s = *impl_;
    auto *target = s.lookup(id);
    if (!target) return false;
    if (s.map->m_TraceStopObj == target) s.map->m_TraceStopObj = TRACE_STOP_NONE;
    s.map->StaticDelete(target);
    s.entities[id - 1] = nullptr;
    return true;
}
void Session::advance() {
    auto &s = *impl_;
    MG_CHECK(s.frames < s.profile.max_frames);
    MG_CHECK(std::chrono::steady_clock::now() - s.started < std::chrono::seconds(20));
#if defined(__MINGW32__)
    MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
    // A single synthetic clip finishes at the next frame boundary. Loops persist.
    for (auto &voice : s.audio_state->voices) if (!voice->loop) voice->playing = false;
    s.map->Takt(s.profile.step_ms);
    s.map->m_Camera.BeforeDraw(); // Refresh CPU view/frustum state as a rendered frame would.
    ++s.frames;
#if defined(__MINGW32__)
    MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
}
int Session::frame() const { return impl_->frames; }
std::uint64_t Session::rejected_icon_targets() const { return impl_->device.stats().rejected_icon_targets; }
int Session::time() const { return impl_->map->GetTime(); }
bool Session::matches(EntityId id, const CMatrixRobotAI *candidate) const {
    return candidate != nullptr && impl_->lookup(id) == candidate;
}
RobotState Session::robot(EntityId id) const {
    auto *r = impl_->lookup(id);
    if (!r) return {id, false, 0, 0, -1, -1, -1, -1};
    const int group = r->GetGroupLogic();
    MG_CHECK(group < MAX_LOGIC_GROUP);
    const auto *order = group >= 0 && r->m_Side == PLAYER_SIDE ? &impl_->side().m_PlayerGroup[group] : nullptr;
    return {id, r->IsLiveRobot(), r->m_PosX, r->m_PosY, group, order ? int(order->Order()) : -1,
            order ? order->m_To.x : -1, order ? order->m_To.y : -1, order ? impl_->id_of(order->m_Obj) : 0,
            !order || order->m_Obj == nullptr, r->PLIsInPlace()};
}
std::string Session::snapshot() const {
    const auto &s = *impl_;
    std::ostringstream out;
    const auto draws = random::draw_count();
    out << "{\"frame\":" << frame() << ",\"time_ms\":" << time() << ",\"rng_draws\":" << draws << ",\"robots\":[";
    for (EntityId id = 1; id <= s.entities.size(); ++id) {
        if (id != 1) out << ',';
        const auto r = robot(id);
        out << "{\"id\":" << id << ",\"alive\":" << (r.alive ? "true" : "false");
        if (r.alive) {
            auto *raw = s.lookup(id);
            out << ",\"x_bits\":" << std::bit_cast<uint32_t>(r.x)
                << ",\"y_bits\":" << std::bit_cast<uint32_t>(r.y)
                << ",\"side\":" << raw->m_Side << ",\"state\":" << int(raw->m_CurrState)
                << ",\"health_bits\":" << std::bit_cast<uint32_t>(raw->GetHitPoint())
                << ",\"in_place\":" << (r.in_place ? "true" : "false")
                << ",\"group\":" << r.group << ",\"order\":" << r.order << ",\"target\":" << r.target
                << ",\"target_is_null\":" << (r.target_is_null ? "true" : "false")
                << ",\"destination\":[" << r.destination_x << ',' << r.destination_y << "],\"queue\":[";
            for (int i = 0; i < raw->GetOrdersInPool(); ++i) {
                if (i) out << ',';
                const auto &order = *raw->GetOrder(i);
                out << '[' << int(order.m_OrderType) << ',' << int(order.m_OrderPhase) << ','
                    << std::bit_cast<uint32_t>(order.m_Param1) << ',' << std::bit_cast<uint32_t>(order.m_Param2)
                    << ',' << s.id_of(order.m_ObjCore ? order.m_ObjCore->m_Object : nullptr) << ']';
            }
            out << ']';
        }
        out << '}';
    }
    out << "],\"player_groups\":[";
    bool first = true;
    for (int group = 0; group < MAX_LOGIC_GROUP; ++group) {
        const auto &order = s.side().m_PlayerGroup[group];
        int members = 0;
        for (auto *live = CMatrixMapStatic::GetFirstLogic(); live; live = live->GetNextLogic())
            if (live->IsRobot() && live->GetSide() == PLAYER_SIDE && live->AsRobot()->GetGroupLogic() == group) ++members;
        if (!members && !order.m_RobotCnt) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"slot\":" << group << ",\"stored_count\":" << order.m_RobotCnt
            << ",\"members\":" << members << ",\"order\":" << int(order.Order())
            << ",\"target\":" << s.id_of(order.m_Obj) << '}';
    }
    out << "]}";
    MG_CHECK(random::draw_count() == draws);
    return out.str();
}
void Session::check_running() const {
    MG_CHECK(impl_->map->GetTime() == impl_->profile.initial_time_ms + frame() * impl_->profile.step_ms);
    MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
    MG_CHECK(impl_->device.stats().material_updates > 0 && impl_->device.stats().locks > 0);
    if (impl_->profile.audio) MG_CHECK(!impl_->audio_state->voices.empty());
}

std::string Session::metadata(std::string_view fixture) const {
    MG_CHECK(!fixture.empty() && fixture.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == std::string_view::npos);
    std::ostringstream out;
    out << "{\"type\":\"header\",\"schema\":1,\"fixture\":\"" << fixture << "\",";
    if (impl_->profile.layout != NavigationLayout::CentralObstacle)
        out << "\"navigation_layout\":\"" << navigation_layout_name(impl_->profile.layout) << "\",";
    out
        << "\"revision\":\"" << MATRIXGAME_SCENARIO_REVISION << "\",\"dirty\":" << MATRIXGAME_SCENARIO_DIRTY << ','
        << "\"render\":\"" << (impl_->profile.unavailable_icon_targets
            ? "storage-device-unavailable-icons-no-draw-cursor-surrogate"
            : "storage-device-no-draw-cursor-surrogate") << "\",\"viewport\":[1280,720],"
        << "\"audio\":\"" << (impl_->profile.audio ? "synthetic-next-frame-completion" : "unavailable") << "\","
        << "\"rng\":\"" << (impl_->profile.mode == random::Mode::ParkMiller ? "ParkMiller" : "LegacyCRT") << "\","
        << "\"seed\":" << impl_->profile.seed << ",\"step_ms\":" << impl_->profile.step_ms
        << ",\"max_frames\":" << impl_->profile.max_frames;
    if (impl_->profile.initial_time_ms) out << ",\"initial_time_ms\":" << impl_->profile.initial_time_ms;
    out << ",\"precision_control\":" << (_controlfp(0, 0) & _MCW_PC)
        << ",\"initially_selected\":" << (impl_->profile.initially_selected ? "true" : "false")
#if MATRIXGAME_SCENARIO_BUILD_DLL
        << ",\"build_exe\":false"
#else
        << ",\"build_exe\":true"
#endif
        << ",\"cheats\":" << (MATRIXGAME_SCENARIO_CHEATS ? "true" : "false")
        << ",\"delayed_damage\":\"" << (DelayedDamage::original() ? "Original" : "Corrected") << '"'
        << ",\"group_counts\":\"" << (GroupCounts::original() ? "Original" : "Corrected") << '"'
        << ",\"place_containment\":\"" << (PlaceContainment::original() ? "Original" : "Corrected") << '"'
        << ",\"zero_x_trace\":\"" << (ZeroXTrace::original() ? "Original" : "Corrected") << '"'
#ifdef ASSERT_OFF
        << ",\"assert_off\":true"
#else
        << ",\"assert_off\":false"
#endif
#ifdef _DEBUG
        << ",\"configuration\":\"Debug\""
#else
        << ",\"configuration\":\"Release\""
#endif
#ifdef _MSC_VER
        << ",\"compiler\":\"MSVC\",\"compiler_version\":" << _MSC_VER
#else
        << ",\"compiler\":\"MinGW\",\"compiler_version\":\"" << __VERSION__ << "\""
#endif
        << '}';
    return out.str();
}

} // namespace tests::battle
