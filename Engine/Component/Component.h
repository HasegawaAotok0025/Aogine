#pragma once
#include "../Core/MathUtility.h" // Vector3/Vector4（位置や色を受け渡すのに使う）
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

// ============================================================================
// Component：オブジェクトに「後から機能を足す」ための土台。
//
// 必要なオブジェクトにだけ、必要な部品を挿す。Unityの GameObject + Component と同じ考え方。
//
//   class Blink : public ComponentOf<Blink> {
//   public:
//       float interval = 0.5f;
//       void OnUpdate(ComponentContext& ctx) override { ... ctx.self を動かす ... }
//   };
//   obj.AddComponent<Blink>()->interval = 0.2f;
//
// ここが持つのは土台だけ：
//   ・Component         … 部品1つの決まりごと
//   ・ComponentOf<T>    … Clone() を自動で用意する中継（部品はこれを継承する）
//   ・ComponentList     … 1つのオブジェクトに付いた部品の束（複製すると中身も複製される）
//   ・ComponentContext  … 部品が呼ばれるときに渡される、周りの状況
//
// 最初から入っている部品は Engine/Components/ 以下にある。
// ============================================================================

struct Object3D;
class ComponentServices;

// 2つの3成分がほぼ同じか。floatの丸めぶんだけ余裕を見る。
// 「自分が前に書いた値と、今の値が違う＝外から書き換えられた」を見分けるのに使う
inline bool NearlySameVector3(const Vector3& a, const Vector3& b) {
    const float kEpsilon = 1e-5f;
    return fabsf(a.x - b.x) < kEpsilon && fabsf(a.y - b.y) < kEpsilon && fabsf(a.z - b.z) < kEpsilon;
}

// 部品を呼ぶときに渡す「周りの状況」。
//
// 部品自身にObject3D*を持たせない のが要点。オブジェクトは std::vector<Object3D> に
// 詰めてあるので、1つ増えただけで全体が別の場所へ引っ越すことがある。
// 毎回ここで渡し直せば、古い場所を指したままになる心配がそもそも生まれない
struct ComponentContext {
    Object3D& self;              // この部品が付いているオブジェクト
    float deltaTime = 0.0f;      // 前のフレームからの経過[秒]
    float timeInScene = 0.0f;    // 起動してからの通算[秒]。波を作るのに使う
    ComponentServices* services = nullptr; // 音・エンジンへの窓口（nullのこともある）
};

// 部品からエンジンの機能を呼ぶための窓口（Engineが実装する）。
// Component.h が Engine.h を読むと、Engine.h → Object3D.h → Component.h と輪になるため、ここを通す
class ComponentServices {
public:
    virtual ~ComponentServices() = default;

    // 効果音を鳴らす。名前はアプリが engine.Sounds().Register() で登録したもの
    virtual void PlayCue(const std::string& cue, float volume) { (void)cue; (void)volume; }
    // 同じシーンの別オブジェクトをタグで1体だけ探す。見つからなければnullptr
    virtual Object3D* FindByTag(const std::string& tag) { (void)tag; return nullptr; }
};

// ===== 触れたことの言葉づかい =====
//   Enter : このステップで触れ始めた
//   Stay  : 前のステップから触れ続けている
//   Exit  : 離れた（このときは接触点も法線も無い）
enum class CollisionPhase { Enter, Stay, Exit };

// 部品へ渡す「触れた」の中身。自分から見た形に直してある（相手＝other、法線＝自分を押し出す向き）
struct ComponentCollision {
    Object3D* other = nullptr;   // 触れた相手（自分ではない方）
    CollisionPhase phase = CollisionPhase::Enter;
    bool isTrigger = false;      // どちらかがトリガー。押し返しは行われていない
    Vector3 point{};             // 代表的な接触点
    Vector3 normal{};            // 自分を押し出す向きの単位法線
    float impulse = 0.0f;        // 押し返しに使った撃力[N・s]。強くぶつかったほど大きい
    float approachSpeed = 0.0f;  // 触れた瞬間の近づく速さ[m/s]。Enter以外は0
};

// ===== 部品1つ =====
class Component {
public:
    virtual ~Component() = default;

    // ログなどに出す名前。書かなくてもよい
    virtual const char* TypeName() const { return "Component"; }

    // 自分と同じ中身の部品を作る。オブジェクトを複製したときに使う。
    // ComponentOf<T>を継承すれば自動で用意されるので、普通は書かなくてよい
    virtual std::unique_ptr<Component> Clone() const = 0;

    // 最初の OnUpdate の直前に1回だけ呼ばれる
    virtual void OnStart(ComponentContext& ctx) { (void)ctx; }

    // 毎フレーム。enabledがfalseなら呼ばれない
    virtual void OnUpdate(ComponentContext& ctx) { (void)ctx; }

    // 何かに触れたとき。触れ始め・触れている間・離れた の3回に分かれて届く。
    // 物理のステップごとに配られるので、1フレームに2回届くことがある。
    // 「触れた回数」を数える用途では、Enterだけを見ること
    virtual void OnCollision(ComponentContext& ctx, const ComponentCollision& hit) { (void)ctx; (void)hit; }

    // この部品を1つのオブジェクトに何個まで付けられるか。falseなら1個まで
    virtual bool AllowsMultiple() const { return false; }

    // オフにすると更新も当たりの通知も止まる（消さずに一時的に止められる）
    bool enabled = true;

private:
    friend class ComponentList;
    bool started_ = false; // OnStart を呼んだか
};

// Clone()を書かなくて済むようにする中継。
//   class Spin : public ComponentOf<Spin> { ... };
template <class T>
class ComponentOf : public Component {
public:
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<T>(static_cast<const T&>(*this));
    }
};

// ===== 1つのオブジェクトに付いた部品の束 =====
//
// unique_ptr の vector を直に持たせると Object3D がコピーできなくなるので、
// コピー（中身の複製）の面倒はこの入れ物だけが引き受ける
class ComponentList {
public:
    ComponentList() = default;
    ~ComponentList() = default;
    ComponentList(const ComponentList& other) { CopyFrom(other); }
    ComponentList& operator=(const ComponentList& other) {
        if (this != &other) { CopyFrom(other); }
        return *this;
    }
    ComponentList(ComponentList&&) noexcept = default;
    ComponentList& operator=(ComponentList&&) noexcept = default;

    bool Empty() const { return items_.empty(); }
    size_t Size() const { return items_.size(); }
    Component* At(size_t index) { return index < items_.size() ? items_[index].get() : nullptr; }
    const Component* At(size_t index) const { return index < items_.size() ? items_[index].get() : nullptr; }

    // 型を指定して1つ取る。付いていなければnullptr
    template <class T>
    T* Get() {
        for (auto& item : items_) {
            if (T* typed = dynamic_cast<T*>(item.get())) { return typed; }
        }
        return nullptr;
    }
    template <class T>
    const T* Get() const { return const_cast<ComponentList*>(this)->Get<T>(); }

    template <class T>
    bool Has() const { return Get<T>() != nullptr; }

    // 型を指定して付ける。すでに付いていて、複数不可の部品なら既存のものを返す
    template <class T, class... Args>
    T* Add(Args&&... args) {
        if (T* existing = Get<T>()) {
            if (!existing->AllowsMultiple()) { return existing; }
        }
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        items_.push_back(std::move(owned));
        return raw;
    }

    void RemoveAt(size_t index) {
        if (index < items_.size()) { items_.erase(items_.begin() + (ptrdiff_t)index); }
    }

    template <class T>
    void Remove() {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const std::unique_ptr<Component>& item) {
                                        return dynamic_cast<T*>(item.get()) != nullptr;
                                    }),
                     items_.end());
    }

    void Clear() { items_.clear(); }

    // 全部の部品を1回ずつ更新する。オフのものは飛ばす。
    // 添字で回すのは、更新の最中に部品が増えても手元が無効にならないようにするため
    //（増えたぶんは次のフレームから動く）
    void Update(ComponentContext& ctx) {
        size_t count = items_.size();
        for (size_t i = 0; i < count && i < items_.size(); ++i) {
            Component* item = items_[i].get();
            if (!item || !item->enabled) { continue; }
            if (!item->started_) {
                item->started_ = true;
                item->OnStart(ctx);
            }
            item->OnUpdate(ctx);
        }
    }

    // 触れたことを全部品へ配る。オフの部品には配らない
    void Collide(ComponentContext& ctx, const ComponentCollision& hit) {
        size_t count = items_.size();
        for (size_t i = 0; i < count && i < items_.size(); ++i) {
            Component* item = items_[i].get();
            if (item && item->enabled) { item->OnCollision(ctx, hit); }
        }
    }

private:
    void CopyFrom(const ComponentList& other) {
        items_.clear();
        items_.reserve(other.items_.size());
        for (const auto& item : other.items_) {
            // 中身まで作り直す。共有にすると、複製した方の値をいじると元まで変わる。
            // 複製した方は「まだ始まっていない」扱いにして、最初の更新で OnStart を受け取らせる
            if (item) {
                std::unique_ptr<Component> copy = item->Clone();
                copy->started_ = false;
                items_.push_back(std::move(copy));
            }
        }
    }

    std::vector<std::unique_ptr<Component>> items_;
};
