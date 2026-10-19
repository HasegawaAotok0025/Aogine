#pragma once
#include <cstdint>
#include "../Engine/Core/MathUtility.h"

class Engine;

// ============================================================================
// Player：キーで動かす箱。WASDで移動、Spaceでジャンプ
//
// 初期化（Initialize）は置いたときに1回、更新（Update）は毎フレーム呼ぶ。
// 箱そのものはエンジンのオブジェクトなので、ここではidだけを控え、使うたびに引き直す
//（物が増えるとエンジン側の並びが引っ越し、参照やポインタは古い場所を指すため）
// ============================================================================
class Player {
public:
    // 箱を置き、物理で動く設定にする
    void Initialize(Engine& engine, const Vector3& position);

    // 入力を見て動かす。落ちたら最初の場所へ戻す
    void Update(float deltaTime);

    // 足元に何かがあるか（ジャンプしてよいか）
    bool IsGrounded() const;

    // 下へ落ちきったか（床の外へ出た）
    bool IsFallen() const;

private:
    Engine* engine_ = nullptr;
    uint32_t id_ = 0;
    Vector3 startPosition_{};
    float coyoteTimer_ = 0.0f; // 足場を離れてからも、まだ跳べる残り時間[秒]

    static constexpr float kMoveSpeed = 4.0f;  // 横に動く速さ[m/s]
    static constexpr float kJumpSpeed = 6.0f;  // 跳ぶ瞬間の上向きの速さ[m/s]
    static constexpr float kFallLimitY = -10.0f; // これより下は落ちたとみなす
    static constexpr float kCoyoteTime = 0.1f;   // 足場を離れてから跳べる猶予[秒]
};
