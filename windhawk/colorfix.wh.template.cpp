// ==WindhawkMod==
// @id              colorfix
// @name            ColorFix
// @description     Phase 1 dark color compatibility for classic Win32 applications
// @version         0.1.0
// @author          ReinierTutoriales
// @include         ColorFixTest.exe
// @architecture    x86-64
// @architecture    x86
// @architecture    arm64
// ==/WindhawkMod==

// @@COLORFIX_CORE@@
// @@COLORFIX_HOOKS@@

static bool WindhawkRegisterHook(HMODULE module, const char* name, void* hook,
                                 void** original) {
    auto target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) { Wh_Log(L"ColorFix: export not found: %S", name); return false; }
    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(L"ColorFix: failed to queue hook: %S", name); return false;
    }
    return true;
}

BOOL Wh_ModInit() {
    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.
    // Wh_ApplyHookOperations is only for hooks queued after initialization.
    return colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook) ? TRUE : FALSE;
}

void Wh_ModUninit() {
    colorfix::hooks::ShutdownPhase1Hooks();
}
