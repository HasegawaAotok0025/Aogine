#include "Player.h"
#include "../Engine/Engine.h"

void Player::Initialize(Engine& engine, const Vector3& position) {
    engine_ = &engine;
    startPosition_ = position;

    Object3D& box = engine.AddCube();
    box.name = "Player";
    box.tag = "Player";
    box.transform.scale = { 0.5f, 0.5f, 0.5f };
    box.transform.translate = position;
    box.usePhysics = true;
    box.rotationMode = RotationMode::UprightLock; // 転ばない
    box.friction = 0.0f;                          // 横の動きはキーで決めるので、床に引きずられない
    box.untextured = true;
    box.materialData->color = { 0.95f, 0.55f, 0.20f, 1.0f };
    id_ = box.id;
}

void Player::Update(float /*deltaTime*/) {
    Object3D* box = engine_ ? engine_->FindById(id_) : nullptr;
    if (!box) { return; }

    // 横の速さはキーでそのまま決める（押していなければ止まる）
    InputEngine& input = engine_->Input();
    float moveX = 0.0f, moveZ = 0.0f;
    if (input.IsKeyDown(DIK_D)) { moveX += 1.0f; }
    if (input.IsKeyDown(DIK_A)) { moveX -= 1.0f; }
    if (input.IsKeyDown(DIK_W)) { moveZ += 1.0f; }
    if (input.IsKeyDown(DIK_S)) { moveZ -= 1.0f; }
    box->velocity.x = moveX * kMoveSpeed;
    box->velocity.z = moveZ * kMoveSpeed;

    // 地面に着いているときだけ跳べる（空中で連続して跳ばないように）
    if (IsGrounded() && input.IsKeyTriggered(DIK_SPACE)) {
        box->velocity.y = kJumpSpeed;
    }

    // 床の外へ落ちたら、最初の場所からやり直す
    if (IsFallen()) {
        box->transform.translate = startPosition_;
        box->velocity = { 0.0f, 0.0f, 0.0f };
    }
}

bool Player::IsGrounded() const {
    const Object3D* box = engine_ ? engine_->FindById(id_) : nullptr;
    return box && box->isGrounded;
}

bool Player::IsFallen() const {
    const Object3D* box = engine_ ? engine_->FindById(id_) : nullptr;
    return box && box->transform.translate.y < kFallLimitY;
}
