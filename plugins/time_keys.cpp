// Time Keys -- Page Up sets the clock to noon, Page Down to midnight, as plugins\mt_plugin_TimeKeys.dll.
//
// The user, 2026-11-12: "when I do page up it's gonna be 12 in the morning ... page down 12 in the night".
// The setting is MotorTownPlayerController.ClientSetTimeOfDay(TimeOfDay hours, Speed, Day) -- what the server calls to
// set a player's clock -- with the current Speed (MTTimeOfDayConfig.TimeOfDayConfig.Speed, 48 in 0.7.20) and Day
// (MTSunSky.Day), so only the hour changes. Tested 2026-11-12: 3:04 -> 12:00, 12:05 -> 00:00, the clock runs on.
// What did not work: Cheat_TimeOfDay (ignored in a shipping build), and writing MTSunSky's SolarTime (only the sky's
// copy, overwritten the next frame).
// Plain keys only: Ctrl+PageUp/PageDown stay the Boats plugin's ballast.
//
// Settings: plugins\time_keys.ini, [General] Disabled; [TimeKeys] DayHour (12), NightHour (0).
// shortcut: sets this machine's clock; on someone else's server their clock comes back with their next sync. Single
// player and the host set the real one.

#include "../include/mt_plugin.hpp"

using namespace mt;

static const wchar_t* INI = L"time_keys.ini";
static uintptr_t cPC, cTodCfg, cSky, fGet, fClientSet, libCDO;
static int oCfg = -1, oSpeed = -1, oSkyDay = -1;
static double kDay, kNight;
static volatile LONG want = -1;
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
template <class T> static T fieldOfFirst(uintptr_t cls, int at, T def) {
    if (!cls || at < 0) return def;
    for (int i = 0, n = api->num_objects(); i < n; i++) {
        uintptr_t o = api->object(i);
        if (o && !isCDO(o) && api->is_a(classOf(o), cls)) return get<T>(o + at);
    }
    return def;
}

static void step(void*) {
    LONG h = InterlockedExchange(&want, -1);
    if (h < 0) return;
    uintptr_t c = playerController();
    if (!c) { MT_LOG("no player controller yet"); return; }
    float speed = fieldOfFirst<float>(cTodCfg, oCfg >= 0 && oSpeed >= 0 ? oCfg + oSpeed : -1, -1);
    int day = fieldOfFirst<int>(cSky, oSkyDay, 0);
    if (speed < 0) { MT_LOG("the clock speed was not found: nothing set (it would change how fast time runs)"); beep(250); return; }
    double before = timeNow(c);
    struct { float tod, speed; int day; } p = {(float)h, speed, day};
    api->process_event(c, fClientSet, &p);
    MT_LOG("time %.2f -> %02ld:00 (speed %.3f, day %d), the game now reads %.2f", before, h, speed, day, timeNow(c));
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
    cTodCfg = api->find_object("Class", "MTTimeOfDayConfig", "/Script/MotorTown");
    cSky = api->find_object("Class", "MTSunSky", "/Script/MotorTown");
    fGet = api->find_object("Function", "GetTimeOfDay", "MTBlueprintLibrary");
    fClientSet = api->find_object("Function", "ClientSetTimeOfDay", "MotorTownPlayerController");
    libCDO = api->find_object("MTBlueprintLibrary", "Default__MTBlueprintLibrary", "/Script/MotorTown");
    if (cTodCfg) oCfg = off(cTodCfg, "TimeOfDayConfig");
    if (uintptr_t sP = api->find_object("ScriptStruct", "MTTimeOfDayConfigParams", "/Script/MotorTown")) oSpeed = off(sP, "Speed");
    if (cSky) oSkyDay = off(cSky, "Day");
    if (!cPC || !fGet || !fClientSet || !libCDO) { MT_LOG("FAILED: time functions not found -- no time keys"); return 0; }
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
        if (h < 0 || !api->game_thread_ready()) continue;
        InterlockedExchange(&want, h);
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
