#pragma once
#include "BaseScene.h"
#include <memory>

class Engine;

// ============================================================================
// SceneManager：今の場面を持ち、毎フレーム呼び、切り替える（Stateパターンの「文脈」）
//
// 場面の切り替えは予約制。ChangeSceneで次の場面を渡しておくと、次のUpdateの頭で
//   今の場面のFinalize → エンジンの物と粒を片付ける → 次の場面のInitialize → 保存した調整を重ねる
// の順に入れ替わる。場面はunique_ptrで持つので、入れ替えた時点で前の場面は自動で消える
// ============================================================================
class SceneManager {
public:
    void Initialize(Engine& engine) { engine_ = &engine; }

    // 次の場面を予約する。すでに予約があれば、新しい方で置き換える
    void ChangeScene(std::unique_ptr<BaseScene> nextScene) { nextScene_ = std::move(nextScene); }

    // 予約があれば切り替えてから、今の場面のUpdateを呼ぶ
    void Update(float deltaTime);

    // 今の場面を片付けて手放す（ゲームを終えるとき）
    void Finalize();

private:
    Engine* engine_ = nullptr;
    std::unique_ptr<BaseScene> currentScene_;
    std::unique_ptr<BaseScene> nextScene_;
};
