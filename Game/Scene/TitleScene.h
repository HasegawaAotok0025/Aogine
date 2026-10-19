#pragma once
#include "BaseScene.h"
#include <cstdint>

// タイトル画面。回る箱と題を出し、Spaceでゲーム本編（GamePlayScene）へ移る
class TitleScene : public BaseScene {
public:
    void Initialize(Engine& engine) override;
    void Update(float deltaTime) override;

private:
    Engine* engine_ = nullptr;
    float elapsed_ = 0.0f; // 案内の文字を点滅させるための経過時間[秒]
};
