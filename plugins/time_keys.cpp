// Time Keys -- Page Up sets the clock to noon, Page Down to midnight, as plugins\mt_plugin_TimeKeys.dll.
//
// The user, 2026-11-12: "when I do page up it's gonna be 12 in the morning ... page down 12 in the night".
// The game's own MotorTownPlayerController.Cheat_TimeOfDay(TimeOfDay) does the setting. Its unit (share of a day,
// hours or minutes) is not documented, so it is learnt from the game's own converter: MTBlueprintLibrary.
// GetTimeOfDayHourMinuteSeconds says which of 0.5 / 12 / 720 reads as 12:00. Plain keys only: Ctrl+PageUp/PageDown
// stay the Boats plugin's ballast.
//
// Settings: plugins\time_keys.ini, [General] Disabled; [TimeKeys] DayHour (12), NightHour (0).
// shortcut: on someone else's server the server owns the clock; the cheat is the host's, so there it may do nothing.

#include "../include/mt_plugin.hpp"

using namespace mt;

static const wchar_t* INI = L"time_keys.ini";
static uintptr_t cPC, fCheat, fGet, fHMS, libCDO;
static double kDay, kNight;
static double unit = -1;  // the value that means 12:00 (0.5, 12 or 720)
static volatile LONG want = -1;  // hour asked for, -1 none
static struct { int idx = -1; uintptr_t o = 0; } pc;

static uintptr_t playerController() {
    if (pc.o && api->object(pc.idx) == pc.o) return pc.o;
    pc.o = 0;
    for (int i = 0, n = api->num_objects(); i < n; i++) {
        uintptr_t o = api->object(i);
        if (o && !isCDO(o) && api->is_a(classOf(o), cPC)) { pc.idx = i; pc.o = o; break; }
    }
    return pc.o;
}

static void beep(int hz, int) {
    CloseHandle(CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { Beep((DWORD)(intptr_t)p, 120); return 0; }, (LPVOID)(intptr_t)hz, 0, nullptr));
}

static int hourOf(uintptr_t ctx, float tod) {
    struct { uintptr_t ctx; float tod; int h, m, s; } p = {ctx, tod, -1, 0, 0};
    api->process_event(libCDO, fHMS, &p);
    return p.h;
}

static void apply(void*) {
    LONG h = InterlockedExchange(&want, -1);
    if (h < 0) return;
    uintptr_t c = playerController();
    if (!c) { MT_LOG("no player controller yet"); return; }
    if (unit < 0) {
        for (double u : {0.5, 12.0, 720.0})
            if (hourOf(c, (float)u) == 12) { unit = u; break; }
        MT_LOG(unit > 0 ? "clock unit: %.1f is noon" : "clock unit not learnt (%.0f): nothing set", unit);
        if (unit < 0) return;
    }
    float tod = (float)(unit * h / 12.0);
    api->process_event(c, fCheat, &tod);
    struct { uintptr_t ctx; float ret; } g = {c, -1};
    if (fGet) api->process_event(libCDO, fGet, &g);
    MT_LOG("time set to %02ld:00 (%.2f), the game now reads %.2f", h, tod, g.ret);
    beep(h == (LONG)kNight ? 500 : 1200, 1);
}

static bool focused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI run(LPVOID) {
    if (!api->wait_engine(INFINITE)) return 0;
    cPC = api->find_object("Class", "MotorTownPlayerController", "/Script/MotorTown");
    fCheat = api->find_object("Function", "Cheat_TimeOfDay", "MotorTownPlayerController");
    fGet = api->find_object("Function", "GetTimeOfDay", "MTBlueprintLibrary");
    fHMS = api->find_object("Function", "GetTimeOfDayHourMinuteSeconds", "MTBlueprintLibrary");
    libCDO = api->find_object("MTBlueprintLibrary", "Default__MTBlueprintLibrary", "/Script/MotorTown");
    if (!cPC || !fCheat || !fHMS || !libCDO) { MT_LOG("FAILED: time functions not found -- no time keys"); return 0; }
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
        if (h >= 0 && api->game_thread_ready()) { InterlockedExchange(&want, h); api->run_on_game_thread(apply, nullptr); }
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
