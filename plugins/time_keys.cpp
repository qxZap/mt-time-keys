// Time Keys -- Page Up sets the clock to noon, Page Down to midnight, as plugins\mt_plugin_TimeKeys.dll.
//
// The user, 2026-11-12: "when I do page up it's gonna be 12 in the morning ... page down 12 in the night".
// First try (Cheat_TimeOfDay) did nothing: a shipping build ignores the cheat (the log showed the game still reading
// 14.2 after "set to 12:00"). So the clock is written where the game keeps it. MTBlueprintLibrary.GetTimeOfDay gives the
// time in hours; the first press looks for that number in MotorTownGameState (float or double), then checks a second
// later which of the matches moved along with the clock -- that one is the time. Then it is written directly.
// Plain keys only: Ctrl+PageUp/PageDown stay the Boats plugin's ballast.
//
// Settings: plugins\time_keys.ini, [General] Disabled; [TimeKeys] DayHour (12), NightHour (0).
// shortcut: the clock found is the one on this machine; on someone else's server their clock comes back with the next
// sync. Single player and the host set the real one.

#include <cmath>
#include <vector>
#include "../include/mt_plugin.hpp"

using namespace mt;

static const wchar_t* INI = L"time_keys.ini";
static uintptr_t fGet, fGetGS, libCDO, gameplayCDO, cPC;
static double kDay, kNight;
static volatile LONG want = -1, phase;  // phase: 0 not looking, 1 candidates taken, 2 found, 3 not found
static int clockAt = -1;
static bool clockDouble;
struct Cand { int at; bool dbl; double was; };
static std::vector<Cand> cands;
static double candT;
static struct { int idx = -1; uintptr_t o = 0; } pc;

static void beep(int hz) {
    CloseHandle(CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { Beep((DWORD)(intptr_t)p, 120); return 0; }, (LPVOID)(intptr_t)hz, 0, nullptr));
}

static uintptr_t playerController() {
    if (pc.o && api->object(pc.idx) == pc.o) return pc.o;
    pc.o = 0;
    for (int i = 0, n = api->num_objects(); i < n; i++) {
        uintptr_t o = api->object(i);
        if (o && !isCDO(o) && api->is_a(classOf(o), cPC)) { pc.idx = i; pc.o = o; break; }
    }
    return pc.o;
}
static double timeNow(uintptr_t ctx) {
    struct { uintptr_t ctx; float ret; } g = {ctx, -1};
    api->process_event(libCDO, fGet, &g);
    return g.ret;
}
static uintptr_t gameState(uintptr_t ctx) {
    struct { uintptr_t ctx; uintptr_t ret; } g = {ctx, 0};
    api->process_event(gameplayCDO, fGetGS, &g);
    return g.ret;
}
static const int GS_SIZE = 2952;  // MotorTownGameState's PropertiesSize (reflection dump, 0.7.20)
static double readAt(uintptr_t gs, const Cand& c) { return c.dbl ? get<double>(gs + c.at) : get<float>(gs + c.at); }

// game thread: phase 0 -> take candidates, phase 1 -> keep the ones that moved with the clock, then write
static void step(void*) {
    uintptr_t c = playerController();
    uintptr_t gs = c ? gameState(c) : 0;
    if (!gs) { MT_LOG("no game state yet"); InterlockedExchange(&phase, 0); return; }
    double t = timeNow(c);
    if (phase == 0) {
        cands.clear();
        for (int at = 0x28; at + 8 <= GS_SIZE; at += 4) {
            float f = get<float>(gs + at);
            if (fabs(f - t) < 0.02) cands.push_back({at, false, f});
            if (at % 8 == 0) { double d = get<double>(gs + at); if (fabs(d - t) < 0.02) cands.push_back({at, true, d}); }
        }
        candT = t;
        MT_LOG("clock %.3f h: %zu place(s) in the game state hold it", t, cands.size());
        InterlockedExchange(&phase, cands.empty() ? 3 : 1);
        if (cands.empty()) { MT_LOG("the clock is not in the game state: time keys off"); beep(250); }
        return;
    }
    if (phase == 1) {
        for (auto& k : cands) {
            double now = readAt(gs, k);
            if (fabs(now - t) < 0.01 && fabs(now - k.was) > 1e-4 && t != candT) { clockAt = k.at; clockDouble = k.dbl; break; }
        }
        if (clockAt < 0) { MT_LOG("none of them moved with the clock (%.3f -> %.3f): time keys off", candT, t); InterlockedExchange(&phase, 3); beep(250); return; }
        MT_LOG("the clock is the %s at +0x%x", clockDouble ? "double" : "float", clockAt);
        InterlockedExchange(&phase, 2);
    }
    LONG h = InterlockedExchange(&want, -1);
    if (h < 0 || clockAt < 0) return;
    if (clockDouble) { double v = h; wr(gs + clockAt, &v, 8); } else { float v = (float)h; wr(gs + clockAt, &v, 4); }
    MT_LOG("time set to %02ld:00, the game now reads %.2f", h, timeNow(c));
    beep(h == (LONG)kNight ? 500 : 1200);
}

static bool focused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI run(LPVOID) {
    if (!api->wait_engine(INFINITE)) return 0;
    cPC = api->find_object("Class", "MotorTownPlayerController", "/Script/MotorTown");
    fGet = api->find_object("Function", "GetTimeOfDay", "MTBlueprintLibrary");
    fGetGS = api->find_object("Function", "GetGameState", "GameplayStatics");
    libCDO = api->find_object("MTBlueprintLibrary", "Default__MTBlueprintLibrary", "/Script/MotorTown");
    gameplayCDO = api->find_object("GameplayStatics", "Default__GameplayStatics", "/Script/Engine");
    if (!cPC || !fGet || !fGetGS || !libCDO || !gameplayCDO) { MT_LOG("FAILED: time functions not found -- no time keys"); return 0; }
    MT_LOG("Time Keys ready: Page Up %02.0f:00, Page Down %02.0f:00", kDay, kNight);
    bool upWas = false, downWas = false;
    for (;;) {
        Sleep(30);
        bool f = focused(), mods = (GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_SHIFT)) & 0x8000;
        bool up = f && !mods && (GetAsyncKeyState(VK_PRIOR) & 0x8000), down = f && !mods && (GetAsyncKeyState(VK_NEXT) & 0x8000);
        LONG h = -1;
        if (up && !upWas) h = (LONG)kDay;
        if (down && !downWas) h = (LONG)kNight;
        upWas = up; downWas = down;
        if (h < 0 || phase == 3 || !api->game_thread_ready()) continue;
        InterlockedExchange(&want, h);
        if (phase == 0) {  // find the clock first: take the candidates, let it run 1.5 s, keep the one that moved
            api->run_on_game_thread(step, nullptr);
            for (int i = 0; i < 100 && phase == 0; i++) Sleep(20);
            if (phase != 1) continue;
            Sleep(1500);
        }
        api->run_on_game_thread(step, nullptr);
    }
}

extern "C" __declspec(dllexport) int PluginInit(const MtApi* a) {
    if (!attach(a)) return 1;
    if (iniDisabled(INI)) return 0;
    kDay = iniNum(INI, L"TimeKeys", L"DayHour", 12);
    kNight = iniNum(INI, L"TimeKeys", L"NightHour", 0);
    CloseHandle(CreateThread(nullptr, 0, run, nullptr, 0, nullptr));
    return 0;
}
