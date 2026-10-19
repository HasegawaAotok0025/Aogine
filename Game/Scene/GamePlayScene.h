#pragma once
#include "BaseScene.h"
#include <cstdint>
#include <memory>

class Player;

// ============================================================================
// ゲーム本編。エンジンの使い方を見せるための小さな見本：
//   床・半透明の箱・Instancingで並べた板・火のようなパーティクル・
//   WASDで動く箱・Mキーで形が切り替わるモデル・絵の一部を切り取ったアイコン
// Escでタイトル（TitleScene）へ戻る
// ============================================================================
class GamePlayScene : public BaseScene {
public:
    GamePlayScene();
    ~GamePlayScene() override; // Playerの中身を知っているcppで片付けるため、ここでは宣言だけ

    void Initialize(Engine& engine) override;
    void Update(float deltaTime) override;
    void Finalize() override;

private:
    Engine* engine_ = nullptr;
    std::unique_ptr<Player> player_; // キーで動かす箱
    uint32_t modelId_ = 0;           // Mキーで形を切り替えるモデル
    bool showingSuzanne_ = true;
};
