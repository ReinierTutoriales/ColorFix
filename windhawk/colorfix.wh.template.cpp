// ==WindhawkMod==
// @id              colorfix
// @name            ColorFix
// @description     Phase 1 dark color compatibility for classic Win32 applications
// @version         0.2.0
// @author          ReinierTutoriales
// @include         ColorFixTest.exe
// @architecture    x86-64
// @architecture    x86
// @architecture    arm64
// @compilerOptions -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- mode: followsystem
  $name: Mode
  $description: When ColorFix applies dark colors. High Contrast always disables it.
  $options:
  - followsystem: Follow the Windows app theme
  - forcedark: Always dark
  - disabled: Disabled
- palette: default
  $name: Palette
  $description: Dark colors used while ColorFix is active.
  $options:
  - default: Default dark (#202020 surfaces)
  - amoled: AMOLED black (#000000 surfaces)
*/
// ==/WindhawkModSettings==

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

// An unrecognized setting value is treated as Disabled: fail safe, never an
// invented preference.
static colorfix::policy::Mode ReadModeSetting() {
    using colorfix::policy::Mode;
    Mode mode = Mode::Disabled;
    if (PCWSTR value = Wh_GetStringSetting(L"mode")) {
        if (lstrcmpiW(value, L"followsystem") == 0) mode = Mode::FollowSystem;
        else if (lstrcmpiW(value, L"forcedark") == 0) mode = Mode::ForceDark;
        else if (lstrcmpiW(value, L"disabled") != 0)
            Wh_Log(L"ColorFix: unknown mode setting, using disabled");
        Wh_FreeStringSetting(value);
    }
    return mode;
}

// An unrecognized palette value is treated as Default: the pre-11a colors.
static colorfix::Palette ReadPaletteSetting() {
    colorfix::Palette palette = colorfix::Palette::Default;
    if (PCWSTR value = Wh_GetStringSetting(L"palette")) {
        if (lstrcmpiW(value, L"amoled") == 0) palette = colorfix::Palette::Amoled;
        else if (lstrcmpiW(value, L"default") != 0)
            Wh_Log(L"ColorFix: unknown palette setting, using default");
        Wh_FreeStringSetting(value);
    }
    return palette;
}

// Mode and palette are read once, together, and applied as one configuration.
static colorfix::runtime::Configuration ReadConfiguration() {
    return colorfix::runtime::Configuration{ReadModeSetting(), ReadPaletteSetting()};
}

BOOL Wh_ModInit() {
    const colorfix::runtime::Configuration config = ReadConfiguration();
    // Publish before any hook is applied: nothing turns dark unless the
    // explicit mode and the real Windows signals say so.
    if (!colorfix::runtime::RefreshPolicy(config).signalsOk)
        Wh_Log(L"ColorFix: reading Windows theme signals failed; staying inactive");
    if (!colorfix::runtime::StartListener(config))
        Wh_Log(L"ColorFix: policy listener failed; theme changes need a restart");
    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.
    // Wh_ApplyHookOperations is only for hooks queued after initialization.
    if (!colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook)) {
        // Windhawk won't call later callbacks when Wh_ModInit returns FALSE.
        colorfix::runtime::StopListener();
        return FALSE;
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    colorfix::runtime::SetConfiguration(ReadConfiguration());
}

void Wh_ModUninit() {
    colorfix::runtime::StopListener();
    colorfix::hooks::ShutdownPhase1Hooks();
}
