#pragma once
#include <Windows.h>
#include <cstdint>

// ============================================================================
// WinAppInitialize：WindowsAPI（COMの初期化・ウィンドウクラスの登録・ウィンドウの生成と表示）をまとめたクラス
// ============================================================================
class WinAppInitialize {
public:
    // ウィンドウプロシージャ
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // COMの初期化、ウィンドウクラスの登録、ウィンドウの生成と表示を行う。
    // fullscreen=trueの場合は「ボーダーレスフルスクリーン」（枠・タイトルバーなしでデスクトップ全体を覆う）にし、
    // 実際に使われた解像度をclientWidth/clientHeightに書き戻す
    void Initialize(const wchar_t* title, int32_t& clientWidth, int32_t& clientHeight, bool fullscreen);

    HWND GetHwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};
};
