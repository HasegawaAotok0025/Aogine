#pragma once

class Engine;
class SceneManager;

// ============================================================================
// BaseScene：場面（タイトル・ゲーム本編など）の基底クラス（Stateパターンの「状態」）
//
// 場面ごとに、このクラスを継承して3つを書く。
//   Initialize … 場面に入ったときに1回。物を置く（前の場面の物はSceneManagerが片付け済み）
//   Update     … 毎フレーム
//   Finalize   … 場面を出るときに1回（物はSceneManagerが片付けるので、ここでは自分で作った物だけ）
//
// 次の場面へ移りたいときは sceneManager_->ChangeScene(...) を呼ぶ。
// その場では切り替わらず、次のフレームの頭で切り替わる（今のUpdateの途中で自分が消えないように）
// ============================================================================
class BaseScene {
public:
    virtual ~BaseScene() = default;

    virtual void Initialize(Engine& engine) = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Finalize() {}

    void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }

protected:
    SceneManager* sceneManager_ = nullptr;
};
