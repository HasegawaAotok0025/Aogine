#pragma once
#include "../Graphics/Object3D.h"
#include <string>

// ============================================================================
// 「反応」の部品。触れたことをきっかけに何かをするもの。
//
//   auto* s = coin.AddComponent<SoundOnHitComponent>();
//   s->cue = "coin";
//   s->onlyTag = "Player";
//
// ■ 当たりは物理のステップごとに届く
//   1フレームに2回届くことがある。「1回だけ」の処理はEnterだけを見ること
// ============================================================================

// ----- 触れたら音を鳴らす -----
// cue は engine.Sounds().Register() で登録した名前（無い名前なら何も起きない）
class SoundOnHitComponent : public ComponentOf<SoundOnHitComponent> {
public:
    const char* TypeName() const override { return "SoundOnHit"; }

    std::string cue = "hit";         // 鳴らす音の名前
    std::string onlyTag;             // この種類（タグ）に触れたときだけ。空なら相手を選ばない
    float minImpulse = 0.3f;         // これより弱い当たりでは鳴らさない
    float volume = 1.0f;
    float cooldownSeconds = 0.08f;   // 続けて鳴らさない間隔

    void OnUpdate(ComponentContext& ctx) override {
        if (cooldown_ > 0.0f) { cooldown_ -= ctx.deltaTime; }
    }

    void OnCollision(ComponentContext& ctx, const ComponentCollision& hit) override {
        if (!ctx.services) { return; }
        if (hit.phase != CollisionPhase::Enter) { return; }
        if (hit.impulse < minImpulse) { return; }
        if (cooldown_ > 0.0f) { return; }
        if (!onlyTag.empty() && (!hit.other || hit.other->tag != onlyTag)) { return; }

        ctx.services->PlayCue(cue, volume);
        cooldown_ = cooldownSeconds;
    }

private:
    float cooldown_ = 0.0f;
};

// ----- 触れたら消える -----
// 拾うもの（コイン・回復）や、踏むと落ちる足場に使う。
// 消すのは見た目と当たり判定だけで、オブジェクト自体は残す
//（当たりを配っている最中に要素を減らすと、他のオブジェクトが引っ越して危ないため）
class VanishOnTouchComponent : public ComponentOf<VanishOnTouchComponent> {
public:
    const char* TypeName() const override { return "VanishOnTouch"; }

    std::string onlyTag;          // 誰が触れたら消えるか。空なら何に触れても
    float respawnSeconds = 0.0f;  // 0で戻らない。正なら、その秒数で戻る

    bool IsHidden() const { return hidden_; }

    void OnUpdate(ComponentContext& ctx) override {
        if (!hidden_ || respawnSeconds <= 0.0f) { return; }
        timer_ += ctx.deltaTime;
        if (timer_ >= respawnSeconds) {
            ctx.self.visible = true;
            ctx.self.useBoxCollider = true;
            hidden_ = false;
            timer_ = 0.0f;
        }
    }

    void OnCollision(ComponentContext& ctx, const ComponentCollision& hit) override {
        if (hidden_) { return; }
        if (hit.phase != CollisionPhase::Enter) { return; }
        if (!onlyTag.empty() && (!hit.other || hit.other->tag != onlyTag)) { return; }

        ctx.self.visible = false;
        ctx.self.useBoxCollider = false;
        hidden_ = true;
        timer_ = 0.0f;
    }

private:
    bool hidden_ = false;
    float timer_ = 0.0f;
};

// ----- 触れている間だけ色が変わる -----
// 離れたら、触れる直前の色に戻す
class HighlightOnTouchComponent : public ComponentOf<HighlightOnTouchComponent> {
public:
    const char* TypeName() const override { return "HighlightOnTouch"; }

    Vector4 touchedColor{ 1.0f, 0.95f, 0.5f, 1.0f };
    std::string onlyTag;

    void OnCollision(ComponentContext& ctx, const ComponentCollision& hit) override {
        if (!ctx.self.materialData) { return; }
        if (!onlyTag.empty() && (!hit.other || hit.other->tag != onlyTag)) { return; }

        if (hit.phase == CollisionPhase::Exit) {
            if (touching_) {
                ctx.self.materialData->color = normalColor_;
                touching_ = false;
            }
            return;
        }
        if (!touching_) {
            // 最初の1回だけ覚える。毎回覚えると「光っている色」を元の色として記録してしまう
            normalColor_ = ctx.self.materialData->color;
            touching_ = true;
        }
        ctx.self.materialData->color = touchedColor;
    }

private:
    Vector4 normalColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool touching_ = false;
};
