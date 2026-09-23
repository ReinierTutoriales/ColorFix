// Backend smoke test: proves MinHook-Detours can inline-hook a real USER32
// export on this architecture. No ColorFix policy involved.
#include <windows.h>
#include <cstdio>
#include "MinHook.h"

using GetSysColor_t = DWORD (WINAPI*)(int);
static GetSysColor_t g_original = nullptr;
static volatile LONG g_calls = 0;
constexpr DWORD kSentinel = RGB(1, 2, 3);

static DWORD WINAPI GetSysColor_Detour(int) {
    InterlockedIncrement(&g_calls);
    return kSentinel;
}

static int Fail(const char* step, MH_STATUS s) {
    std::printf("FAIL %s: %s\n", step, MH_StatusToString(s));
    return 1;
}

int main() {
    auto target = reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSysColor"));
    if (!target) { std::puts("FAIL GetProcAddress"); return 1; }

    const DWORD before = GetSysColor(COLOR_WINDOW);
    MH_STATUS s;
    if ((s = MH_Initialize()) != MH_OK) return Fail("MH_Initialize", s);
    if ((s = MH_CreateHook(target, reinterpret_cast<void*>(GetSysColor_Detour),
                           reinterpret_cast<void**>(&g_original))) != MH_OK)
        return Fail("MH_CreateHook", s);
    if ((s = MH_EnableHook(MH_ALL_HOOKS)) != MH_OK) return Fail("MH_EnableHook", s);

    const DWORD hooked = GetSysColor(COLOR_WINDOW);
    const DWORD passthrough = g_original(COLOR_WINDOW);

    if ((s = MH_DisableHook(MH_ALL_HOOKS)) != MH_OK) return Fail("MH_DisableHook", s);
    const DWORD after = GetSysColor(COLOR_WINDOW);
    MH_Uninitialize();

    const bool ok = hooked == kSentinel && g_calls == 1 &&
                    passthrough == before && after == before;
    std::printf("%s before=%06lX hooked=%06lX trampoline=%06lX after=%06lX calls=%ld\n",
                ok ? "PASS" : "FAIL", before, hooked, passthrough, after, g_calls);
    return ok ? 0 : 1;
}
