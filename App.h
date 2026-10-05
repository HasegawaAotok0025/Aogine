#pragma once

class Engine; // 前方宣言（ここではEngine.hを読み込まなくてよいようにする）

// ============================================================================
// ゲームの入口
//
// エンジンとmain.cppが知っているのは、この2つの関数だけ。
// 実装は Game/Game.cpp にある。ゲームのコードは Game/ 以下に書き、Engine/ には手を入れない。
// ============================================================================

// 起動直後に1回呼ばれる。オブジェクトを置き、設定を決め、
// 毎フレームの処理を engine.SetUpdateCallback() で渡す
void SetupScene(Engine& engine);

// 起動設定
struct AppStartup {
    const wchar_t* windowTitle = L"Aogine";

    // 全画面で始めるか。コマンドラインに -window を付けると、どちらでも窓(1280x720)になる
    bool fullscreen = false;

    // 画面へ書き出すときに、リニアからsRGBへ直すか（普通は true のまま）
    bool convertToSrgbOnOutput = true;
};

AppStartup GetAppStartup();
