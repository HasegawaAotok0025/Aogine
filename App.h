#pragma once

// ============================================================================
// 起動設定。ゲーム（Game::GetStartup）が返し、main.cppがエンジンを作るときに使う
// ============================================================================
struct AppStartup {
    const wchar_t* windowTitle = L"Aogine";

    // 全画面で始めるか。コマンドラインに -window を付けると、どちらでも窓(1280x720)になる
    bool fullscreen = false;

    // 画面へ書き出すときに、リニアからsRGBへ直すか（普通は true のまま）
    bool convertToSrgbOnOutput = true;
};
