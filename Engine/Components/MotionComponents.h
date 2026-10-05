#pragma once
#include "../Graphics/Object3D.h"

// ============================================================================
// 「動き」の部品。オブジェクトのtransformを毎フレーム動かすもの。
// どれも物理を通さない（決まった通りに動けばよいものは、力で動かす必要がない）。
//
//   obj.AddComponent<SpinComponent>()->degreesPerSecond = 90.0f;
//
// ■ 位置を動かす部品（Bob / Patrol）の中心
//   付けた時点の位置が中心（出発点）になる。
//   ゲーム側のコードで位置を書き換えると、そこが新しい中心になる
//   （自分が最後に書いた位置と違えば「外から動かされた」とみなす）。
// ============================================================================

// ----- 回し続ける -----
class SpinComponent : public ComponentOf<SpinComponent> {
public:
    const char* TypeName() const override { return "Spin"; }

    Vector3 axis{ 0.0f, 0.0f, 1.0f }; // 回る軸。{0,0,1}で画面の中で回る
    float degreesPerSecond = 90.0f;

    void OnUpdate(ComponentContext& ctx) override {
        const float kTwoPi = 6.28318530718f;
        float radians = degreesPerSecond * 0.01745329252f * ctx.deltaTime;
        Vector3& rotate = ctx.self.transform.rotate;
        rotate.x = Wrap(rotate.x + axis.x * radians, kTwoPi);
        rotate.y = Wrap(rotate.y + axis.y * radians, kTwoPi);
        rotate.z = Wrap(rotate.z + axis.z * radians, kTwoPi);
    }

private:
    // 一周ぶん引いて範囲に収める。放っておくとfloatの桁が足りなくなり、回転がかくつき始める
    static float Wrap(float value, float period) {
        while (value > period) { value -= period; }
        while (value < -period) { value += period; }
        return value;
    }
};

// ----- 上下にふわふわ浮かぶ -----
class BobComponent : public ComponentOf<BobComponent> {
public:
    const char* TypeName() const override { return "Bob"; }

    float amplitude = 0.25f;               // 振れ幅[m]
    float cyclesPerSecond = 0.5f;          // 1秒に何往復するか
    float phase = 0.0f;                    // 複数並べたときに動きをずらす[0..1]
    Vector3 direction{ 0.0f, 1.0f, 0.0f }; // 揺れる向き。既定は真上

    void OnUpdate(ComponentContext& ctx) override {
        Vector3& translate = ctx.self.transform.translate;
        if (!hasBase_ || !NearlySameVector3(translate, lastWritten_)) {
            basePosition_ = translate;
            hasBase_ = true;
        }

        elapsed_ += ctx.deltaTime;
        float wave = sinf((elapsed_ * cyclesPerSecond + phase) * 6.28318530718f) * amplitude;
        translate = {
            basePosition_.x + direction.x * wave,
            basePosition_.y + direction.y * wave,
            basePosition_.z + direction.z * wave,
        };
        lastWritten_ = translate;
    }

private:
    Vector3 basePosition_{ 0.0f, 0.0f, 0.0f };
    Vector3 lastWritten_{ 0.0f, 0.0f, 0.0f };
    bool hasBase_ = false;
    float elapsed_ = 0.0f;
};

// ----- 決めた幅を往復する -----
class PatrolComponent : public ComponentOf<PatrolComponent> {
public:
    const char* TypeName() const override { return "Patrol"; }

    Vector3 offset{ 3.0f, 0.0f, 0.0f }; // 出発点からどこまで行くか
    float secondsOneWay = 2.0f;         // 片道にかける時間
    float holdSeconds = 0.4f;           // 端で止まる時間
    bool smooth = true;                 // 端で滑らかに折り返す（falseで等速）

    void OnUpdate(ComponentContext& ctx) override {
        Vector3& translate = ctx.self.transform.translate;
        if (!hasBase_ || !NearlySameVector3(translate, lastWritten_)) {
            basePosition_ = translate;
            hasBase_ = true;
            elapsed_ = 0.0f;
        }

        float oneWay = (secondsOneWay > 0.01f) ? secondsOneWay : 0.01f;
        float hold = (holdSeconds < 0.0f) ? 0.0f : holdSeconds;
        float period = (oneWay + hold) * 2.0f;
        elapsed_ = fmodf(elapsed_ + ctx.deltaTime, period);

        // 往路 → 端で待つ → 復路 → 端で待つ
        float t = 0.0f;
        if (elapsed_ < oneWay)                    { t = elapsed_ / oneWay; }
        else if (elapsed_ < oneWay + hold)        { t = 1.0f; }
        else if (elapsed_ < oneWay * 2.0f + hold) { t = 1.0f - (elapsed_ - oneWay - hold) / oneWay; }
        else                                      { t = 0.0f; }

        float eased = smooth ? (t * t * (3.0f - 2.0f * t)) : t;
        translate = {
            basePosition_.x + offset.x * eased,
            basePosition_.y + offset.y * eased,
            basePosition_.z + offset.z * eased,
        };
        lastWritten_ = translate;
    }

private:
    Vector3 basePosition_{ 0.0f, 0.0f, 0.0f };
    Vector3 lastWritten_{ 0.0f, 0.0f, 0.0f };
    bool hasBase_ = false;
    float elapsed_ = 0.0f;
};
