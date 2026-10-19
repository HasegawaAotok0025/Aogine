#include "WinAppInitialize.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

LRESULT CALLBACK WinAppInitialize::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) { return true; }
#endif
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WinAppInitialize::Initialize(const wchar_t* title, int32_t& clientWidth, int32_t& clientHeight, bool fullscreen) {
    CoInitializeEx(0, COINIT_MULTITHREADED);

    wc_.lpfnWndProc = WindowProc;
    wc_.lpszClassName = L"CG2WindowClass";
    wc_.hInstance = GetModuleHandle(nullptr);
    // 窓の上のマウスカーソル。指定しないと、窓の外で最後に出ていた形（読み込み中の輪など）のまま残る
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc_);

    if (fullscreen) {
        clientWidth = GetSystemMetrics(SM_CXSCREEN);
        clientHeight = GetSystemMetrics(SM_CYSCREEN);
        // WS_POPUP：タイトルバー・枠なし。位置(0,0)・デスクトップと同じ大きさにすることで全画面に見せる。
        // WS_EX_APPWINDOW を付けるのは、タスクバーに必ず出すため。
        // 枠なしで画面を覆っているので、これが無いと他の窓へ移った後で戻る手立てが分かりにくい
        hwnd_ = CreateWindowEx(WS_EX_APPWINDOW, wc_.lpszClassName, title, WS_POPUP,
            0, 0, clientWidth, clientHeight, nullptr, nullptr, wc_.hInstance, nullptr);
    } else {
        RECT wrc = { 0, 0, clientWidth, clientHeight };
        AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
        hwnd_ = CreateWindow(wc_.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            wrc.right - wrc.left, wrc.bottom - wrc.top, nullptr, nullptr, wc_.hInstance, nullptr);
    }
    ShowWindow(hwnd_, SW_SHOW);
}
