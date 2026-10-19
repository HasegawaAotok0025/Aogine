// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#include "Engine/Engine.h"
#include "Game/Game.h"

// エントリーポイント：エンジンとゲームの初期化・実行・終了を呼ぶだけ。ゲームの中身は Game/ 以下に書く
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int) {
    D3DResourceLeakChecker leakChecker; // 一番最初に置くことで、一番最後にリソースリークをチェックする

    Game game;
    AppStartup startup = game.GetStartup();
    // "-window" を付けて起動すると、全画面ではなく既定の大きさの窓で出る
    bool fullscreen = startup.fullscreen && !(commandLine && strstr(commandLine, "-window") != nullptr);

    Engine engine;
    engine.Initialize(startup.windowTitle, WinAppInitialize::kDefaultClientWidth, WinAppInitialize::kDefaultClientHeight,
                      fullscreen, startup.convertToSrgbOnOutput ? SurfaceColor::Srgb : SurfaceColor::AsIs);
    game.Initialize(engine);

    engine.Run();

    game.Finalize();
    engine.Shutdown();
    return 0;
}
