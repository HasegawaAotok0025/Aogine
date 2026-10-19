#include "Game.h"
#include "Scene/TitleScene.h"
#include "../Engine/Engine.h"

AppStartup Game::GetStartup() const {
    AppStartup startup; // 既定値は App.h（題 "Aogine"・窓で起動・sRGBへ直す）
    // 変えたいものだけここで上書きする。例：startup.fullscreen = true;
    return startup;
}

void Game::Initialize(Engine& engine) {
    sceneManager_.Initialize(engine);
    // 最初の場面。実際に入るのは最初のUpdateの頭
    sceneManager_.ChangeScene(std::make_unique<TitleScene>());

    // 毎フレームの処理はこのクラスのUpdateに任せる
    engine.SetUpdateCallback([this](float deltaTime) { Update(deltaTime); });
}

void Game::Update(float deltaTime) {
    sceneManager_.Update(deltaTime);
}

void Game::Finalize() {
    sceneManager_.Finalize();
}
