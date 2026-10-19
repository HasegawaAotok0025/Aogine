#include "TitleScene.h"
#include "GamePlayScene.h"
#include "SceneManager.h"
#include "../../Engine/Engine.h"

void TitleScene::Initialize(Engine& engine) {
    engine_ = &engine;
    elapsed_ = 0.0f;

    engine.GetCamera().SetPosition({ 0.0f, 1.5f, -6.0f });
    engine.GetCamera().LookAt({ 0.0f, 0.5f, 0.0f });

    // 題の後ろでゆっくり回る箱（部品の見本：SpinComponentを挿すだけで回る）
    Object3D& cube = engine.AddCube();
    cube.name = "TitleCube";
    cube.transform.translate = { 0.0f, 0.5f, 0.0f };
    cube.untextured = true;
    cube.materialData->color = { 0.95f, 0.55f, 0.20f, 1.0f };
    SpinComponent* spin = cube.AddComponent<SpinComponent>();
    spin->axis = { 0.0f, 1.0f, 0.0f }; // 縦の軸で回す
    spin->degreesPerSecond = 45.0f;
}

void TitleScene::Update(float deltaTime) {
    elapsed_ += deltaTime;

    engine_->DrawTextCentered(200.0f, "Aogine", 96.0f);
    // 案内は1秒ごとに点滅させる
    if (std::fmod(elapsed_, 1.0f) < 0.6f) {
        engine_->DrawTextCentered(520.0f, "Space で始める", 32.0f);
    }

    if (engine_->Input().IsKeyTriggered(DIK_SPACE)) {
        sceneManager_->ChangeScene(std::make_unique<GamePlayScene>());
    }
}
