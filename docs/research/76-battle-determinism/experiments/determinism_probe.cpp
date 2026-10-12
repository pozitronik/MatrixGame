// Throwaway research probe for issue #76. Not part of the repository.
// Drives production random helpers and production effect code on a synthetic
// map with no device, window or resources, and prints traces for comparison.
#include "MatrixMap.hpp"
#include "Effects/MatrixEffect.hpp"
#include "Effects/MatrixEffectSmokeAndFire.hpp"
#include "Effects/MatrixEffectWeapon.hpp"
#include "MatrixSoundManager.hpp"
#include "SoundBridge.hpp"
#include "MatrixGameDll.hpp"
#include "Helper.hpp"
#include "audio_fixture.hpp"
#include "random.hpp"

#include <cinttypes>
#include <float.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint64_t PM_M = 2147483647ULL;
uint64_t pm_pow(uint64_t b, uint64_t e) {
    uint64_t r = 1;
    b %= PM_M;
    while (e) {
        if (e & 1) r = r * b % PM_M;
        b = b * b % PM_M;
        e >>= 1;
    }
    return r;
}
const uint64_t PM_INV = pm_pow(16807, PM_M - 2);

// Observes the hidden Park-Miller state through the public API only:
// one draw reveals the next state; seed() restores the previous one exactly.
struct DrawIndex {
    std::unordered_map<uint32_t, uint32_t> index;
    void build(uint32_t seed, uint32_t n) {
        index.clear();
        uint64_t s = seed % PM_M;
        if (!s) s = 1;
        for (uint32_t i = 0; i <= n; ++i) {
            index.emplace(uint32_t(s), i);
            s = s * 16807 % PM_M;
        }
    }
    long now() {
        const uint64_t next = uint64_t(random::Rnd()) + 1;
        const uint64_t cur = next * PM_INV % PM_M;
        random::seed(uint32_t(cur));
        auto it = index.find(uint32_t(cur));
        return it == index.end() ? -1 : long(it->second);
    }
};

struct Fnv {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void *p, size_t n) {
        auto *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    }
    template<class T> void add(const T &v) { bytes(&v, sizeof(v)); }
};

template<class Object>
struct EngineHeapDeleter {
    void operator()(Object *object) const { HDelete(Object, object, g_MatrixHeap); }
};

struct World {
    CHeap heap;
    CBlockPar data;
    std::unique_ptr<CMatrixMapLogic, EngineHeapDeleter<CMatrixMapLogic>> map;
    World() {
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        CacheInit();
        map.reset(HNew(g_MatrixHeap) CMatrixMapLogic);
        g_MatrixMap = map.get();
    }
    ~World() {
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
        g_MatrixMap = nullptr;
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
    }
};

} // namespace

// Read-only access to CMatrixEffectSmoke through the friend declaration added by
// smoke_observer.patch. Callers pass only effects whose GetType() is EFFECT_SMOKE,
// so the downcast names the object's actual dynamic type.
struct SmokeObserver {
    static uint64_t hash(CMatrixEffect *e, int *count, float *maxz);
    static void dump(CMatrixEffect *e, int t);
};

namespace {
uint64_t smoke_hash(CMatrixEffect *e, int *count, float *maxz) {
    return SmokeObserver::hash(e, count, maxz);
}
} // namespace

uint64_t SmokeObserver::hash(CMatrixEffect *e, int *count, float *maxz) {
    const auto *s = static_cast<const CMatrixEffectSmoke *>(e);
    Fnv f;
    f.add(s->m_PuffCnt);
    f.add(s->m_Time);
    f.add(s->m_NextSpawnTime);
    float mz = -1e30f;
    for (int i = 0; i < s->m_PuffCnt; ++i) {
        const auto &p = s->m_Puffs[i];
        f.add(p.m_PuffAngle);
        f.add(p.m_PuffTTL);
        f.add(p.m_Puff.m_Pos);
        float sc = p.m_Puff.GetScale();
        f.add(sc);
        if (p.m_Puff.m_Pos.z > mz) mz = p.m_Puff.m_Pos.z;
    }
    *count = s->m_PuffCnt;
    *maxz = mz;
    return f.h;
}

void SmokeObserver::dump(CMatrixEffect *e, int t) {
    const auto *sm = static_cast<const CMatrixEffectSmoke *>(e);
    auto bits = [](float v) { uint32_t u; std::memcpy(&u, &v, 4); return u; };
    std::printf("dump t=%d m_Time=%08x m_TTL=%08x next=%08x\n", t, bits(sm->m_Time), bits(sm->m_TTL), bits(sm->m_NextSpawnTime));
    for (int i = 0; i < sm->m_PuffCnt; ++i) {
        const auto &p = sm->m_Puffs[i];
        std::printf("dump puff=%d angle=%08x ttl=%08x z=%08x scale=%08x\n", i, bits(p.m_PuffAngle), bits(p.m_PuffTTL),
                    bits(p.m_Puff.m_Pos.z), bits(p.m_Puff.GetScale()));
    }
}

namespace {
int effect_count() {
    int n = 0;
    for (auto *e = g_MatrixMap->m_EffectsFirst; e; e = e->m_Next) ++n;
    return n;
}

// The production effect loop from CMatrixMap::Takt, restricted to effects.
void effects_takt(float step) {
    for (PCMatrixEffect e = g_MatrixMap->m_EffectsFirst; e != NULL;) {
        g_MatrixMap->m_EffectsNextTakt = e->m_Next;
        e->Takt(step);
        e = g_MatrixMap->m_EffectsNextTakt;
    }
}

std::vector<int> schedule(const std::string &name, int total) {
    std::vector<int> s;
    if (name == "jitter") {
        // Deterministic frame lengths in 1..100 ms, independent of the engine stream.
        uint32_t x = 2463534242u;
        int t = 0;
        while (t < total) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int d = 1 + int(x % 100);
            if (t + d > total) d = total - t;
            s.push_back(d);
            t += d;
        }
        return s;
    }
    const int d = std::stoi(name);
    for (int t = 0; t < total; t += d) s.push_back(std::min(d, total - t));
    return s;
}

// smoke <schedule> [lasers=N] [far] [pause] [checkpoint=ms]
int run_smoke(int argc, char **argv) {
    const std::string sched = argv[2];
    int lasers = 0;
    bool distant = false, paused = false;
    int checkpoint = 400;
    int dumpat = -1;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("lasers=", 0) == 0) lasers = std::stoi(a.substr(7));
        else if (a == "far") distant = true;
        else if (a == "pause") paused = true;
        else if (a == "pc24") _control87(_PC_24, _MCW_PC);
        else if (a.rfind("checkpoint=", 0) == 0) checkpoint = std::stoi(a.substr(11));
        else if (a.rfind("dumpat=", 0) == 0) dumpat = std::stoi(a.substr(7));
    }
    World world;
    const uint32_t seed = 20261012;
    random::seed(seed, random::Mode::ParkMiller);
    DrawIndex idx;
    idx.build(seed, 2000000);
    const D3DXVECTOR3 origin(distant ? 4000.0f : 100.0f, 50.0f, 0.0f);
    std::vector<CLaser *> laser_list;
    for (int i = 0; i < lasers; ++i) {
        auto *l = HNew(g_MatrixHeap) CLaser(D3DXVECTOR3(0, 0, 0), D3DXVECTOR3(10, 0, 0));
        if (!g_MatrixMap->AddEffect(l)) return 3;
        laser_list.push_back(l);
    }
    if (paused) SETFLAG(g_MatrixMap->m_Flags, MMFLAG_PAUSE);
    CMatrixEffect::CreateSmoke(nullptr, origin, 2500, 700, 45, 0xFF808080);
    CMatrixEffect *smoke = nullptr;
    for (auto *e = g_MatrixMap->m_EffectsFirst; e; e = e->m_Next)
        if (e->GetType() == EFFECT_SMOKE) smoke = e;
    std::printf("created smoke=%d effects=%d draw=%ld\n", smoke ? 1 : 0, effect_count(), idx.now());
    const auto steps = schedule(sched, 3000);
    int t = 0, frame = 0;
    int next_cp = checkpoint;
    for (int step : steps) {
        // Render stand-in: production CLaser::BeforeDraw once per drawn frame.
        for (auto *l : laser_list) l->BeforeDraw();
        // Simulation stand-in: one production effect pass per loop iteration.
        effects_takt(float(step));
        t += step;
        ++frame;
        smoke = nullptr;
        for (auto *e = g_MatrixMap->m_EffectsFirst; e; e = e->m_Next)
            if (e->GetType() == EFFECT_SMOKE) smoke = e;
        int cnt = 0;
        float maxz = 0;
        const uint64_t h = smoke ? smoke_hash(smoke, &cnt, &maxz) : 0;
        const long d = idx.now();
        std::printf("frame t=%d step=%d draw=%ld puffs=%d maxz=%.9g hash=%016" PRIx64 "\n", t, step, d, cnt, maxz, h);
        if (smoke && t == dumpat)
            SmokeObserver::dump(smoke, t);
        if (t >= next_cp) {
            std::printf("checkpoint t=%d draw=%ld puffs=%d maxz=%.9g hash=%016" PRIx64 "\n", t, d, cnt, maxz, h);
            next_cp += checkpoint;
        }
    }
    std::printf("final frames=%d effects=%d draw=%ld\n", frame, effect_count(), idx.now());
    return 0;
}

// rng <ParkMiller|LegacyCRT>: helper sequence hashes and draw counts.
int run_rng(const std::string &m) {
    const auto mode = m == "LegacyCRT" ? random::Mode::LegacyCRT : random::Mode::ParkMiller;
    struct Helper { const char *name; void (*fn)(Fnv &); };
    const Helper helpers[] = {
        {"Rnd()", [](Fnv &f) { f.add(random::Rnd()); }},
        {"RndFloat()", [](Fnv &f) { f.add(random::RndFloat()); }},
        {"RND(-3.7,11.2)", [](Fnv &f) { f.add(RND(-3.7, 11.2)); }},
        {"RND(0,M_PI)", [](Fnv &f) { f.add(RND(0, M_PI)); }},
        {"FRND(5.5)", [](Fnv &f) { f.add(FRND(5.5)); }},
        {"FRND(128)", [](Fnv &f) { f.add(FRND(128)); }},
        {"FSRND(M_PI)", [](Fnv &f) { f.add(FSRND(M_PI)); }},
        {"IRND(7)", [](Fnv &f) { f.add(IRND(7)); }},
        {"Rnd(-10,10)", [](Fnv &f) { f.add(random::Rnd(-10, 10)); }},
        {"Rnd(INT_MIN,INT_MAX)", [](Fnv &f) { f.add(random::Rnd(INT_MIN, INT_MAX)); }},
    };
    for (const auto &h : helpers) {
        random::seed(12345, mode);
        Fnv f;
        for (int i = 0; i < 1000000; ++i) h.fn(f);
        const int tail = random::Rnd();
        std::printf("%s %s hash=%016" PRIx64 " next_raw=%d\n", m.c_str(), h.name, f.h, tail);
    }
    return 0;
}

// sound <on|off>: draws consumed by non-positional CSound::Play.
int run_sound(bool on) {
    Base::CBlockPar data;
    g_MatrixData = &data;
    g_RangersInterface = nullptr;
    auto *event = data.BlockGetAdd(L"Sounds")->BlockGetAdd(L"bclick");
    event->ParSetAdd(L"path", L"Sound.A");
    event->ParSetAdd(L"vol", L"0.5,1");
    event->ParSetAdd(L"pan", L"-0.5,0.5");
    CSound::Init();
    auto device = std::make_shared<audio_fixture::DeviceState>();
    if (on) {
        SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(device),
            Audio::Mapping{{L"Sound.A", L"synthetic.wav"}},
            [](const std::wstring &) { return audio_fixture::wave(); },
            [](const std::wstring &, const std::string &) {}));
    }
    const uint32_t seed = 777;
    random::seed(seed, random::Mode::ParkMiller);
    DrawIndex idx;
    idx.build(seed, 1000);
    const long before = idx.now();
    for (int i = 0; i < 10; ++i) (void)CSound::Play(S_BCLICK, SL_ALL);
    const long after = idx.now();
    const int next = random::Rnd();
    std::printf("sound available=%d draws_for_10_plays=%ld next_raw=%d\n", SoundBridge::available() ? 1 : 0,
                after - before, next);
    CSound::Clear();
    SoundBridge::shutdown();
    g_MatrixData = nullptr;
    return 0;
}

// crt: a foreign std::rand() call shifts the LegacyCRT stream; the state is not observable.
int run_crt() {
    random::seed(5, random::Mode::LegacyCRT);
    int a1 = random::Rnd(), a2 = random::Rnd();
    random::seed(5, random::Mode::LegacyCRT);
    int b1 = random::Rnd();
    (void)std::rand();
    int b2 = random::Rnd();
    std::printf("crt clean=%d,%d with_foreign_rand=%d,%d\n", a1, a2, b1, b2);
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    int r = 2;
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "rng" && argc > 2) r = run_rng(argv[2]);
        else if (mode == "smoke" && argc > 2) r = run_smoke(argc, argv);
        else if (mode == "sound" && argc > 2) r = run_sound(std::string(argv[2]) == "on");
        else if (mode == "crt") r = run_crt();
        else std::fprintf(stderr, "usage: rng MODE | smoke SCHEDULE [lasers=N] [far] [pause] | sound on|off | crt\n");
    }
    catch (const std::exception &e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        r = 1;
    }
    catch (...) {
        std::fprintf(stderr, "engine exception\n");
        r = 1;
    }
    std::fflush(stdout);
    Base::CMain::BaseDeInit();
    return r;
}
