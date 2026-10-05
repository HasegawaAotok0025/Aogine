// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#include "Engine/Engine.h"
#include "App.h"

// エントリーポイント：初期化・配置・実行・終了の呼び出しだけを行う。
// ゲームの中身は Game/ 以下（SetupScene / GetAppStartup）に書く
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int) {
    D3DResourceLeakChecker leakChecker; // 一番最初に置くことで、一番最後にリソースリークをチェックする

    AppStartup startup = GetAppStartup();

    // "-window" を付けて起動すると、全画面ではなく1280x720の窓で出る
    bool fullscreen = startup.fullscreen;
    if (commandLine && strstr(commandLine, "-window") != nullptr) { fullscreen = false; }

    Engine engine;
    // 全画面のときはデスクトップの解像度で起動する（1280,720は窓のときの大きさ）
    engine.Initialize(startup.windowTitle, 1280, 720, fullscreen,
                      startup.convertToSrgbOnOutput ? SurfaceColor::Srgb : SurfaceColor::AsIs);

    SetupScene(engine);

    engine.Run();
    engine.Shutdown();

    return 0;
}
