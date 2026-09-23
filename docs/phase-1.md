# Phase 1: classic Win32 color interception

## Objective

Validate semantic system-color interception before UxTheme or modern rendering APIs.

## Hooks

- USER32: `GetSysColor`, `GetSysColorBrush`
- GDI32: `GetStockObject`, `SetTextColor`, `SetBkColor`, `CreateSolidBrush`

## Invariants

- Preserve special/non-RGB `COLORREF` values.
- Prefer semantic system roles over literal RGB transformation.
- Never call a hooked entry point internally when the original trampoline is available.
- Brushes returned in place of `GetSysColorBrush` are process-lifetime objects because callers must not delete system brushes.
- Hook installation is queued and committed as a batch.
- Phase 1 does not hook UxTheme, GDI+, DirectWrite, or Direct2D.

## Test matrix

| Windows host | Process | Phase |
|---|---|---|
| Windows 11 x64 | x64 native | MVP |
| Windows 11 x64 | x86/WOW64 | MVP |
| Windows 11 ARM64 | ARM64 native | MVP |
| Windows 11 ARM64 | x64 emulated | later |
| Windows 11 ARM64 | x86 emulated | later |

## Known limitation

`GetStockObject` has no semantic role parameter. Mapping `WHITE_BRUSH` or
`BLACK_BRUSH` is necessarily heuristic and will need an application/process
policy switch before production use.
