#include <windows.h>
#include <commctrl.h>

namespace {
constexpr wchar_t kClassName[] = L"ColorFixTestWindow";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"Classic Win32 controls",
            WS_CHILD | WS_VISIBLE, 20, 20, 260, 24, hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Edit control",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20, 55, 260, 28, hwnd, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Button",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            20, 95, 120, 32, hwnd, reinterpret_cast<HMENU>(1002), nullptr, nullptr);

        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT,
            20, 145, 420, 170, hwnd, reinterpret_cast<HMENU>(1003), nullptr, nullptr);
        LVCOLUMNW col{LVCF_TEXT | LVCF_WIDTH};
        col.pszText = const_cast<LPWSTR>(L"ListView");
        col.cx = 380;
        ListView_InsertColumn(list, 0, &col);
        LVITEMW item{LVIF_TEXT};
        item.iItem = 0;
        item.pszText = const_cast<LPWSTR>(L"Semantic and GDI hook validation");
        ListView_InsertItem(list, &item);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kClassName, L"ColorFix Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 500, 390,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
