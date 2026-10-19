#include "SceneManager.h"
#include "../../Engine/Engine.h"

void SceneManager::Update(float deltaTime) {
    if (nextScene_) {
        if (currentScene_) { currentScene_->Finalize(); }
        // 前の場面が置いた物と粒を、次の場面を置く前にその場で片付ける
        engine_->ClearScene();

        currentScene_ = std::move(nextScene_);
        currentScene_->SetSceneManager(this);
        currentScene_->Initialize(*engine_);
        // デバッグビュー（F2）で調整して保存した位置などを、新しく置いた物にも重ねる
        engine_->ApplySavedEdits();
    }

    if (currentScene_) { currentScene_->Update(deltaTime); }
}

void SceneManager::Finalize() {
    if (currentScene_) { currentScene_->Finalize(); }
    currentScene_.reset();
    nextScene_.reset();
}
