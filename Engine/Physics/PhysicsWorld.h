// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL Baumgarte
#pragma once
#include <cfloat>
#include <unordered_set>
#include "../Graphics/Object3D.h"

// ============================================================================
// 衝突イベント：物理が見つけた接触を、ゲーム側へ知らせるための情報
//
// ソルバーは毎ステップ全ての接触を把握しているのに、それが外へ出ていないと
// 「ゴールに着いた」「着地音を鳴らす」「ダメージを受けた」といった、ゲームそのものが作れない。
// そこで接触を3つの瞬間に分けて配る（Unityの OnCollisionEnter/Stay/Exit と同じ考え方）。
//
//   Enter … 今フレーム新しく触れた。効果音・ダメージ・得点など「1回だけ」の処理はここ
//   Stay  … 前フレームから触れ続けている。乗っている間の処理（滑る床など）はここ
//   Exit  … 離れた。接触点はもう無いので、点や法線は最後に触れていたときの値が入る
//
// トリガー（isTrigger）の場合も同じイベントが届く。違いは押し返されないことだけ
// ============================================================================
struct CollisionEvent {
    CollisionPhase phase = CollisionPhase::Enter;
    Object3D* a = nullptr;      // 触れた2つ。どちらがa/bになるかは決まっていないので、両方を見て判断すること
    Object3D* b = nullptr;
    bool isTrigger = false;     // どちらかがトリガーで、押し返しは行われていない
    Vector3 point{};            // 代表的な接触点（複数点で触れている場合はその平均）
    Vector3 normal{};           // bからaへ向かう単位法線（aを押し出す向き）
    float impulse = 0.0f;       // 押し返しに使った撃力の合計[N・s]。強くぶつかったほど大きい。
                                // 着地音の音量や、壊れる/壊れないの閾値にそのまま使える
    float approachSpeed = 0.0f; // 触れた瞬間の、法線方向へ近づく速さ[m/s]。Enter以外では0

    // ----- 判定を書きやすくするための小道具 -----
    // a/bのどちらが目的の相手かは決まっていないため、素直に書くと毎回2通りの条件を並べることになる。
    // 「ゴールに触れたか」を1行で書けるようにしておく

    // どちらかがこのタグを持っているか
    bool Involves(const std::string& targetTag) const {
        return (a && a->tag == targetTag) || (b && b->tag == targetTag);
    }
    // このタグを持っている側を返す（無ければnullptr）
    Object3D* WithTag(const std::string& targetTag) const {
        if (a && a->tag == targetTag) { return a; }
        if (b && b->tag == targetTag) { return b; }
        return nullptr;
    }
    // 自分ではない方（相手）を返す。selfがこの衝突に関係していなければnullptr
    Object3D* Other(const Object3D* self) const {
        if (self == a) { return b; }
        if (self == b) { return a; }
        return nullptr;
    }
};

// ============================================================================
// PhysicsWorld：剛体の物理シミュレーション（逐次インパルス法）
//
// ■ 何をしているか
//   1. 接触点を集める（コンタクトマニフォールド生成）
//   2. 重力を速度に加える
//   3. 各接触点について「めり込む向きに近づく相対速度」を打ち消す撃力を求め、繰り返し解く
//   4. どこがどれだけの強さで触れたかをイベントとしてゲーム側へ知らせる
//   5. 速度で位置と姿勢を進める
//   6. 十分止まった物を眠らせる
//
// ■ 衝突イベント
//   4は物理計算そのものには不要だが、ここでしか分からない情報を外へ渡すために要る。
//   「ゴールに着いた」「強く着地した」といったゲームの進行は、位置を毎フレーム自前で調べるのではなく、
//   すでに接触を把握しているこちらから知らせる方が正確で速い。Events()で取り出す
//
// ■ なぜ「撃力（インパルス）」なのか
//   剛体どうしの衝突は、ごく短時間に大きな力が働く現象で、力×時間＝撃力としてまとめて扱う。
//   撃力Jを重心からrだけ離れた接触点に加えると、
//       速度   Δv = J / m
//       角速度 Δω = I⁻¹ (r × J)
//   の2つが同時に変化する。この「同時に変わる」ことが剛体の本質で、
//   角から落ちた箱が角を支点にパタンと倒れるのも、重心が接触点からずれているために
//   接触の撃力が回転を生むという、ただそれだけの帰結として自然に出てくる。
//   （以前のように「トルクだけ足す」「回転の基点をずらす」といった後付けの補正は一切要らない）
//
// ■ 慣性テンソル
//   回転の起こしやすさは軸ごとに違う。細長い物は長手方向まわりには回りやすく、
//   横倒し方向には回りにくい。これを表すのが慣性モーメントで、Object3Dが形状から計算する。
//   計算はローカル軸基準なので、ワールドの量を扱うときは
//   「ワールド→ローカルへ戻す → 逆慣性を掛ける → ワールドへ戻す」の3手順で変換する。
//
// ■ 逐次インパルス法（Sequential Impulses）
//   接触点が複数あると、1点ずつ解いても他の点の条件が崩れる。そこで全接触点を何度も
//   繰り返し解いて全体を釣り合わせる。各点の撃力を累積して「引っ張る向きにはならない」よう
//   クランプするのが要点で、これにより箱が床の上でガタつかずに静止する。
//
// ■ 呼び出し方
//   Step()は固定の時間刻み（1/60秒）で呼ぶこと。可変のdeltaTimeをそのまま積分すると、
//   フレームレートが変わるだけで跳ね方や止まる位置が変わり、再現性が無くなる。
// ============================================================================
class PhysicsWorld {
public:
    // ===== ワールド設定（コードから直接触れるようpublicにしてある） =====
    float gravity = -9.8f;          // m/s^2（Yが上方向）
    float linearDamping = 0.05f;    // 空気抵抗。0にすると水平方向にいつまでも進み続ける
    float angularDamping = 0.05f;   // 回転の空気抵抗
    int solverIterations = 10;      // 接触の解き直し回数。多いほど正確だが重い

    // 1ステップぶん時間を進める
    void Step(float deltaTime, std::vector<Object3D>& objects) {
        if (deltaTime <= 0.0f) { return; }
        sceneObjects_ = &objects;

        SyncOrientationFromEuler(objects);   // ゲームのコードで回転をいじられていたら取り込む
        RefreshMeshCaches(objects);          // メッシュコライダーのワールド三角形をこのフレーム用に用意
        BuildContacts(objects);              // 接触点を集める
        IntegrateVelocities(objects, deltaTime);
        PrepareContacts(deltaTime);
        SolveContacts();
        EmitCollisionEvents();               // 解き終わった撃力を添えて、接触をゲーム側へ知らせる
        IntegratePositions(objects, deltaTime);
        UpdateSleeping(objects, deltaTime);
        WriteBackEuler(objects);             // 表示・当たり判定用にオイラー角へ戻す

        sceneObjects_ = nullptr;
    }

    // 直前のStep()で起きた衝突イベント。Step()を呼ぶたびに中身は作り直される。
    // 固定タイムステップなので1フレームに複数回Step()が回ることがあり、
    // 取りこぼさないようにStep()の直後に必ず読むこと（Engine::Runがそうしている）
    const std::vector<CollisionEvent>& Events() const { return events_; }

    // 静止メッシュコライダーが「今フレーム誰かと衝突しているか」（ワイヤーフレームの緑/赤表示に使う）
    const std::vector<bool>& MeshCollidingFlags() const { return meshCollidingFlags_; }

    // シーンを組み替えたときなど、接触の履歴を捨てて「何にも触れていない」状態から始め直す。
    // これをしないと、消えたオブジェクトとの接触が残ったままExitイベントが遅れて飛ぶことがある
    void ClearContactHistory() {
        touchingPairs_.clear();
        pairHistory_.clear();
        events_.clear();
    }

private:
    // 接触の計算に必要な剛体の情報を、ソルバーを回す前に一度だけ取り出しておく。
    // 逆慣性テンソルは形状と姿勢から決まり、撃力を解いている間は変化しない。
    // 形状からの計算には行列構築と平方根が含まれるので、反復のたびにやり直すのは純粋な無駄になる
    struct BodyProxy {
        Object3D* obj = nullptr;
        float invMass = 0.0f;
        Vector3 invInertia{};   // ローカル軸ごとの慣性モーメントの逆数
        Vector3 axis[3]{};      // ローカル軸がワールドでどちらを向いているか
        Vector3 com{};          // 重心のワールド座標
    };

    // ===== 1つの接触点 =====
    // 「aをbから引き離す向き」をnormalとし、bはaの相手（静止オブジェクトなら逆質量0で動かない）
    struct Contact {
        Object3D* a = nullptr;
        Object3D* b = nullptr;
        Vector3 point{};        // ワールド空間の接触点
        Vector3 normal{};       // bからaへ向かう単位法線
        float penetration = 0.0f;

        // ソルバーが使う事前計算値
        BodyProxy bodyA, bodyB;
        Vector3 rA{}, rB{};          // 各重心から接触点までのベクトル（撃力の腕）
        Vector3 tangent[2]{};        // 接触面に沿った2方向（摩擦用）
        float normalMass = 0.0f;     // 法線方向の有効質量の逆数
        float tangentMass[2]{};
        float normalImpulse = 0.0f;  // 累積撃力。マイナス（引っ張る向き）にならないようクランプする
        float tangentImpulse[2]{};
        float velocityBias = 0.0f;   // 反発＋めり込み解消のための目標速度
        float friction = 0.5f;
        float restitution = 0.0f;
        float approachSpeed = 0.0f;  // ぶつかってきた速さ（法線方向・負が近づく向き）。イベントに載せる
    };

    // 押し返さないトリガーとの重なり。解決はしないので、位置と法線だけ控えておく
    struct TriggerOverlap {
        Object3D* a = nullptr;
        Object3D* b = nullptr;
        Vector3 point{};
        Vector3 normal{};
    };

    // 1組（2オブジェクト）ぶんの接触をまとめたもの。同じ組に接触点が4つあっても、
    // ゲーム側へは「その組が触れている」という1件として渡したいので、ここで束ねる
    struct PairInfo {
        Object3D* a = nullptr;
        Object3D* b = nullptr;
        uint32_t idA = 0, idB = 0;
        Vector3 pointSum{};
        int pointCount = 0;
        Vector3 normal{};
        float impulse = 0.0f;          // その組の全接触点の合計
        float strongestImpulse = 0.0f; // 1点あたりの最大。代表の法線をどの点から取るか決めるのに使う
        float approachSpeed = 0.0f;
        bool isTrigger = false;
    };

    std::vector<Object3D>* sceneObjects_ = nullptr; // Step()の間だけ有効なシーンへの参照
    std::vector<Contact> contacts_;
    std::vector<TriggerOverlap> triggerOverlaps_;

    // ===== 衝突イベント =====
    std::vector<CollisionEvent> events_;
    // 前のステップで触れていた組。ここに在るかどうかでEnterとStayを見分ける
    std::unordered_set<uint64_t> touchingPairs_;
    // 触れていた組の最後の情報。離れた瞬間（Exit）を伝えるとき、接触点はもう存在しないため
    // 最後に触れていたときの値を使う
    std::unordered_map<uint64_t, PairInfo> pairHistory_;

    std::vector<bool> meshCollidingFlags_;

    // ===== メッシュコライダーのワールド三角形/空間分割グリッドのキャッシュ =====
    // 三角形化とグリッド構築はそれなりに重いため、transformが実際に変化したときだけ作り直す。
    // 地面のような静止オブジェクトは初回以降ずっとキャッシュを使い回すことになり、負荷が大きく下がる
    std::vector<std::vector<Triangle>> meshTriangleCache_;
    std::vector<TriangleGrid> meshGridCache_;
    std::vector<Transform> meshCacheTransform_;
    std::vector<bool> meshCacheValid_;
    std::vector<OBB> meshBroadOBB_;
    std::vector<bool> hasMeshCollider_;

    // 法線に垂直な2方向を作る（摩擦の向き）。どの向きでもよいので、成分が最も小さい軸との外積から作る
    static void BuildTangentBasis(const Vector3& normal, Vector3& outT0, Vector3& outT1) {
        Vector3 seed = (fabsf(normal.x) < 0.57735f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
        outT0 = Normalize(Cross(seed, normal), { 1.0f, 0.0f, 0.0f });
        outT1 = Cross(normal, outT0);
    }

    // ===== 剛体の基本操作 =====

    // ソルバー用の情報を取り出す
    static BodyProxy MakeBodyProxy(Object3D& obj) {
        OBB obb = obj.GetWorldOBB();
        BodyProxy proxy{};
        proxy.obj = &obj;
        proxy.invMass = obj.GetInverseMass();
        proxy.invInertia = obj.GetInverseInertiaLocal();
        proxy.axis[0] = obb.axis[0];
        proxy.axis[1] = obb.axis[1];
        proxy.axis[2] = obb.axis[2];
        proxy.com = obb.center;
        return proxy;
    }

    // ワールドのベクトルに逆慣性テンソルを掛ける。
    // 慣性テンソルはローカル軸（形状に固定された軸）を基準に対角化されているので、
    //   I⁻¹v = Σ_k (1/I_k)(v・a_k) a_k     ※a_k はローカル軸のワールド方向
    // として、軸ごとに射影→割り算→戻す、の3回で求まる
    static Vector3 ApplyInverseInertia(const BodyProxy& body, const Vector3& worldVector) {
        const float invI[3] = { body.invInertia.x, body.invInertia.y, body.invInertia.z };
        Vector3 result{ 0.0f, 0.0f, 0.0f };
        for (int k = 0; k < 3; ++k) {
            if (invI[k] == 0.0f) { continue; } // 拘束された軸（無限の慣性）
            result = Add(result, Scale(body.axis[k], invI[k] * Dot(worldVector, body.axis[k])));
        }
        return result;
    }

    // 撃力Jを、重心からrだけ離れた点に加える。
    // 並進（Δv = J/m）と回転（Δω = I⁻¹(r×J)）が同時に変わるのが剛体の要点
    static void ApplyImpulse(const BodyProxy& body, const Vector3& impulse, const Vector3& r) {
        if (body.invMass <= 0.0f) { return; } // 静止オブジェクトは無限に重いので動かない

        Object3D& obj = *body.obj;
        obj.velocity = Add(obj.velocity, Scale(impulse, body.invMass));
        obj.angularVelocity = Add(obj.angularVelocity, ApplyInverseInertia(body, Cross(r, impulse)));
    }

    // 重心からrだけ離れた点の速度（並進＋回転による周速度）
    static Vector3 PointVelocity(const BodyProxy& body, const Vector3& r) {
        return Add(body.obj->velocity, Cross(body.obj->angularVelocity, r));
    }

    // 方向n・腕rにおける「有効質量」の逆数。
    //   k = 1/m + (r×n)・I⁻¹(r×n)
    // 回転しにくい向き（拘束された軸や、慣性モーメントの大きい軸）ほど小さくなり、重く感じる
    static float EffectiveMassTerm(const BodyProxy& body, const Vector3& r, const Vector3& n) {
        if (body.invMass <= 0.0f) { return 0.0f; }
        Vector3 rn = Cross(r, n);
        return body.invMass + Dot(rn, ApplyInverseInertia(body, rn));
    }

    // 摩擦係数・反発係数を2物体から合成する。摩擦は幾何平均、反発は大きい方（一般的な取り決め）
    static float CombineFriction(const Object3D& a, const Object3D& b) { return sqrtf((std::max)(0.0f, a.friction * b.friction)); }
    static float CombineRestitution(const Object3D& a, const Object3D& b) { return (std::max)(a.restitution, b.restitution); }

    // ===== オイラー角との同期 =====

    // ゲームのコードがtransform.rotateを直接書き換えていたら、姿勢へ取り込む。
    // 「前回こちらが書いた値」と違っていれば外部からの編集とみなす方式なので、
    // 書き換えている箇所を一つ残らず探して回る必要がない（見落としによる不整合が起きない）
    static void SyncOrientationFromEuler(std::vector<Object3D>& objects) {
        for (auto& obj : objects) {
            const Vector3& e = obj.transform.rotate;
            const Vector3& s = obj.syncedEuler;
            if (e.x != s.x || e.y != s.y || e.z != s.z) {
                obj.orientation = QuaternionFromEulerXYZ(e);
                obj.syncedEuler = e;
            }
        }
    }

    // 姿勢を表示・描画用のオイラー角へ戻す。描画も当たり判定もMakeAffineMatrix経由なので、
    // ここさえ合わせておけば他のコードは一切クォータニオンを知らなくてよい
    static void WriteBackEuler(std::vector<Object3D>& objects) {
        for (auto& obj : objects) {
            if (obj.isStatic || !obj.usePhysics) { continue; }
            obj.transform.rotate = EulerXYZFromQuaternion(obj.orientation);
            obj.syncedEuler = obj.transform.rotate;
        }
    }

    // ===== 接触点の生成 =====

    // OBB同士の接触点を集める。
    // 面で当たっていれば4点、辺で当たっていれば2点、角なら1点、辺どうしの交差なら1点と、
    // 実際の支え方どおりの数が返る。1点しか作らないと、面で寝ているはずの箱がガタガタ揺れてしまう
    void CollectBoxContacts(Object3D& a, Object3D& b) {
        OBB obbA = a.GetWorldOBB();
        OBB obbB = b.GetWorldOBB();

        SATResult sat;
        if (!ComputeSAT(obbA, obbB, sat)) { return; } // 離れている

        // 接触点の求め方はOBBの幾何だけで決まる話なので、BoxColliderに任せる。
        // 面で当たっていれば面を切り取って複数点、辺どうしなら最接近点1点が返る
        Vector3 points[kMaxManifoldPoints];
        float depths[kMaxManifoldPoints];
        Vector3 normal;
        int count = BuildContactManifold(obbA, obbB, sat, points, depths, normal);
        for (int i = 0; i < count; ++i) {
            AddContact(a, b, points[i], normal, depths[i]);
        }
    }

    void AddContact(Object3D& a, Object3D& b, const Vector3& point, const Vector3& normal, float penetration) {
        Contact contact{};
        contact.a = &a;
        contact.b = &b;
        contact.point = point;
        contact.normal = normal;
        contact.penetration = penetration;
        contact.friction = CombineFriction(a, b);
        contact.restitution = CombineRestitution(a, b);
        contacts_.push_back(contact);

        // ほぼ真上を向いた法線で押し返されている＝何かの上に乗っている。ジャンプ可否の判定に使う
        if (normal.y > 0.5f) { a.isGrounded = true; }
        if (normal.y < -0.5f) { b.isGrounded = true; }
    }

    // 重なり判定に参加できる形を持っているか。トリガーは押し返しに使わないためuseBoxColliderがOFFでも構わないが、
    // 形そのものは必要なので、常にOBBを形状として使う
    static bool HasOverlapShape(const Object3D& obj) { return obj.useBoxCollider || obj.isTrigger; }

    // トリガーとの重なりを1件だけ控える。押し返さないので接触点を細かく求める必要はなく、
    // 「どこで重なったか」の目安として2つの重心の中点を使う（効果音を鳴らす位置などに十分な精度）
    void CollectTriggerOverlap(Object3D& a, Object3D& b) {
        OBB obbA = a.GetWorldOBB();
        OBB obbB = b.GetWorldOBB();
        if (!Intersect(obbA, obbB)) { return; }

        TriggerOverlap overlap{};
        overlap.a = &a;
        overlap.b = &b;
        overlap.point = Scale(Add(obbA.center, obbB.center), 0.5f);
        overlap.normal = Normalize(Sub(obbA.center, obbB.center), { 0.0f, 1.0f, 0.0f });
        triggerOverlaps_.push_back(overlap);
    }

    // このフレームぶんの接触点をすべて集める
    void BuildContacts(std::vector<Object3D>& objects) {
        contacts_.clear();
        triggerOverlaps_.clear();
        meshCollidingFlags_.assign(objects.size(), false);
        for (auto& obj : objects) {
            if (!obj.isStatic) { obj.isGrounded = false; } // 接地判定はこのあとの接触生成で立て直す
        }

        // --- シーン内のオブジェクト同士 ---
        // 同じ組を2回調べないよう j > i の組み合わせだけを見る（撃力は両方へ同時に働くので1回で足りる）
        for (size_t i = 0; i < objects.size(); ++i) {
            for (size_t j = i + 1; j < objects.size(); ++j) {
                Object3D& a = objects[i];
                Object3D& b = objects[j];
                // どちらも動かないなら調べる意味がない
                if (a.GetInverseMass() <= 0.0f && b.GetInverseMass() <= 0.0f) { continue; }
                // スプライトは画面の板（座標は画面の上の値）で、3Dの世界には居ない。
                // レイや影と同じく、物理でも相手にしない
                if (a.isSprite || b.isSprite) { continue; }

                // トリガーが絡む組は、形状の種類にかかわらず「重なっているか」だけを見る。
                // 押し返さない以上、接触点を精密に求める意味がないので、粗いOBB判定で十分
                if (a.isTrigger || b.isTrigger) {
                    if (HasOverlapShape(a) && HasOverlapShape(b)) { CollectTriggerOverlap(a, b); }
                    continue;
                }

                if (hasMeshCollider_[j] && !hasMeshCollider_[i]) { CollectMeshContacts(a, b, j, i); continue; }
                if (hasMeshCollider_[i] && !hasMeshCollider_[j]) { CollectMeshContacts(b, a, i, j); continue; }
                if (hasMeshCollider_[i] && hasMeshCollider_[j]) {
                    // 複雑な形状同士は、動く側の頂点を相手の三角形に対して判定する
                    if (a.GetInverseMass() > 0.0f) { CollectMeshContacts(a, b, j, i); }
                    else { CollectMeshContacts(b, a, i, j); }
                    continue;
                }

                if (!a.useBoxCollider || !b.useBoxCollider) { continue; }
                CollectBoxContacts(a, b);
            }
        }
    }

    // 動く側(mover)と、メッシュコライダーを持つ相手(mesh)の接触。
    // メッシュ側は静止専用（Unityと同じ設計）なので、接触点は1つだけ作れば十分
    void CollectMeshContacts(Object3D& mover, Object3D& mesh, size_t meshIndex, size_t moverIndex) {
        if (mover.GetInverseMass() <= 0.0f) { return; }

        if (mover.colliderShape == ColliderShape::Sphere) {
            float radius = mover.GetSphereRadius();
            Vector3 center = mover.GetCenterOfMass();
            if (!IntersectSphereOBB(center, radius, meshBroadOBB_[meshIndex])) { return; } // 粗い判定で絞り込む

            Vector3 pushedCenter = center;
            Vector3 normal, contactPoint;
            if (ResolveSphereVsMesh(pushedCenter, radius, meshTriangleCache_[meshIndex], meshGridCache_[meshIndex], normal, contactPoint)) {
                float penetration = sqrtf(LengthSq(Sub(pushedCenter, center)));
                meshCollidingFlags_[meshIndex] = true;
                AddContact(mover, mesh, contactPoint, normal, penetration);
            }
            return;
        }

        // 複雑な形状：自分の実頂点群を相手の三角形へ当てる（スザンヌ対スザンヌなど）
        if (!mover.useMeshCollider || mover.localPositions.empty()) {
            // メッシュを持たない箱なら、相手のOBBを箱とみなした簡易判定にする
            if (mover.useBoxCollider && mesh.useBoxCollider) { CollectBoxContacts(mover, mesh); }
            return;
        }
        if (!Intersect(mover.GetWorldOBB(), meshBroadOBB_[meshIndex])) { return; }

        Matrix4x4 world = MakeAffineMatrix(mover.transform.scale, mover.transform.rotate, mover.transform.translate);
        std::vector<Vector3> worldVerts;
        worldVerts.reserve(mover.localPositions.size());
        for (const auto& p : mover.localPositions) { worldVerts.push_back(TransformPoint(p, world)); }

        Vector3 pushOffset{}, normal{}, contactPoint{};
        if (ResolveVerticesVsMesh(worldVerts, mover.meshVertexRadius, meshTriangleCache_[meshIndex], meshGridCache_[meshIndex], pushOffset, normal, contactPoint)) {
            meshCollidingFlags_[meshIndex] = true;
            meshCollidingFlags_[moverIndex] = true;
            AddContact(mover, mesh, contactPoint, normal, sqrtf(LengthSq(pushOffset)));
        }
    }

    // メッシュコライダーのワールド三角形・グリッド・広域判定用OBBをこのフレーム用に用意する
    void RefreshMeshCaches(std::vector<Object3D>& objects) {
        if (meshTriangleCache_.size() != objects.size()) {
            meshTriangleCache_.resize(objects.size());
            meshGridCache_.resize(objects.size());
            meshCacheTransform_.resize(objects.size());
            // オブジェクトが増減するとインデックスが1つずつずれ、キャッシュが「別のオブジェクトの三角形」を
            // 指したままになる可能性がある。数が変わったときは全部作り直させるのが確実
            meshCacheValid_.assign(objects.size(), false);
        }
        meshBroadOBB_.assign(objects.size(), OBB{});
        hasMeshCollider_.assign(objects.size(), false);

        for (size_t k = 0; k < objects.size(); ++k) {
            Object3D& s = objects[k];
            if (!s.useMeshCollider || s.localPositions.empty()) { continue; }
            if (!meshCacheValid_[k] || !TransformEquals(meshCacheTransform_[k], s.transform)) {
                Matrix4x4 world = MakeAffineMatrix(s.transform.scale, s.transform.rotate, s.transform.translate);
                meshTriangleCache_[k] = BuildWorldTriangles(s.localPositions, s.triangleIndices, world);
                meshGridCache_[k] = BuildTriangleGrid(meshTriangleCache_[k]);
                meshCacheTransform_[k] = s.transform;
                meshCacheValid_[k] = true;
            }
            meshBroadOBB_[k] = s.GetWorldOBB();
            hasMeshCollider_[k] = true;
        }
    }

    static bool TransformEquals(const Transform& a, const Transform& b) {
        return a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z &&
               a.rotate.x == b.rotate.x && a.rotate.y == b.rotate.y && a.rotate.z == b.rotate.z &&
               a.translate.x == b.translate.x && a.translate.y == b.translate.y && a.translate.z == b.translate.z;
    }

    // ===== 速度の積分（外力）=====
    void IntegrateVelocities(std::vector<Object3D>& objects, float deltaTime) {
        for (auto& obj : objects) {
            if (obj.isStatic) { continue; }
            if (!obj.usePhysics) {
                // 物理をOFFにしたら運動エネルギーを持ち越さない（ONにした瞬間に過去の速度で動き出すのを防ぐ）
                obj.velocity = { 0.0f, 0.0f, 0.0f };
                obj.angularVelocity = { 0.0f, 0.0f, 0.0f };
                continue;
            }

            obj.velocity.y += gravity * deltaTime;

            float linear = (std::max)(0.0f, 1.0f - linearDamping * deltaTime);
            float angular = (std::max)(0.0f, 1.0f - angularDamping * deltaTime);
            obj.velocity = Scale(obj.velocity, linear);
            obj.angularVelocity = Scale(obj.angularVelocity, angular);
        }
    }

    // ===== 接触の事前計算 =====
    void PrepareContacts(float deltaTime) {
        // めり込みを解消するために与える速度の割合。1.0にすると1ステップで一気に押し出して跳ね上がるため、
        // 少しずつ返す。slopは「この程度のめり込みは許す」という余裕で、これが無いと接触が付いたり離れたりして震える
        const float kBaumgarte = 0.2f;
        const float kPenetrationSlop = 0.005f;
        // 低速の接触にまで反発を効かせると、床に置いた物が細かく跳ね続ける。
        // これより遅い衝突は「そっと触れた」とみなして反発させない
        const float kRestitutionThreshold = 1.0f;

        for (Contact& c : contacts_) {
            c.bodyA = MakeBodyProxy(*c.a);
            c.bodyB = MakeBodyProxy(*c.b);

            c.rA = Sub(c.point, c.bodyA.com);
            c.rB = Sub(c.point, c.bodyB.com);
            BuildTangentBasis(c.normal, c.tangent[0], c.tangent[1]);

            float kn = EffectiveMassTerm(c.bodyA, c.rA, c.normal) + EffectiveMassTerm(c.bodyB, c.rB, c.normal);
            c.normalMass = (kn > 1e-12f) ? 1.0f / kn : 0.0f;
            for (int t = 0; t < 2; ++t) {
                float kt = EffectiveMassTerm(c.bodyA, c.rA, c.tangent[t]) + EffectiveMassTerm(c.bodyB, c.rB, c.tangent[t]);
                c.tangentMass[t] = (kt > 1e-12f) ? 1.0f / kt : 0.0f;
            }

            // めり込みを押し返すぶんの目標速度
            float positionBias = (kBaumgarte / deltaTime) * (std::max)(0.0f, c.penetration - kPenetrationSlop);

            // 反発：ぶつかってきた速さの一部を跳ね返す
            Vector3 relative = Sub(PointVelocity(c.bodyA, c.rA), PointVelocity(c.bodyB, c.rB));
            float approachSpeed = Dot(relative, c.normal);
            float restitutionBias = 0.0f;
            if (approachSpeed < -kRestitutionThreshold) { restitutionBias = -c.restitution * approachSpeed; }
            c.approachSpeed = -approachSpeed; // イベントには「近づいてきた速さ」を正の値で載せる

            c.velocityBias = (std::max)(positionBias, restitutionBias);
            c.normalImpulse = 0.0f;
            c.tangentImpulse[0] = 0.0f;
            c.tangentImpulse[1] = 0.0f;
        }
    }

    // ===== 接触の解決（逐次インパルス）=====
    // 全接触点を繰り返し解き、「どこもめり込む向きへ進んでいない」状態へ収束させる
    void SolveContacts() {
        for (int iteration = 0; iteration < solverIterations; ++iteration) {
            for (Contact& c : contacts_) {
                // --- 摩擦（先に解く。垂直抗力は前の反復の値を使う）---
                for (int t = 0; t < 2; ++t) {
                    Vector3 relative = Sub(PointVelocity(c.bodyA, c.rA), PointVelocity(c.bodyB, c.rB));
                    float vt = Dot(relative, c.tangent[t]);
                    float lambda = -vt * c.tangentMass[t];

                    // クーロン摩擦：働ける力の上限は μ×垂直抗力。これを超える力は出せず、物は滑り出す
                    float maxFriction = c.friction * c.normalImpulse;
                    float oldImpulse = c.tangentImpulse[t];
                    c.tangentImpulse[t] = (std::max)(-maxFriction, (std::min)(maxFriction, oldImpulse + lambda));
                    float applied = c.tangentImpulse[t] - oldImpulse;

                    Vector3 impulse = Scale(c.tangent[t], applied);
                    ApplyImpulse(c.bodyA, impulse, c.rA);
                    ApplyImpulse(c.bodyB, Scale(impulse, -1.0f), c.rB);
                }

                // --- 法線方向（めり込みを止める）---
                Vector3 relative = Sub(PointVelocity(c.bodyA, c.rA), PointVelocity(c.bodyB, c.rB));
                float vn = Dot(relative, c.normal);
                float lambda = (-vn + c.velocityBias) * c.normalMass;

                // 累積撃力が負にならないようクランプする。
                // これが無いと接触点が「物を引き寄せる」動きをしてしまい、面で寝ている箱が震え出す
                float oldImpulse = c.normalImpulse;
                c.normalImpulse = (std::max)(0.0f, oldImpulse + lambda);
                float applied = c.normalImpulse - oldImpulse;

                Vector3 impulse = Scale(c.normal, applied);
                ApplyImpulse(c.bodyA, impulse, c.rA);
                ApplyImpulse(c.bodyB, Scale(impulse, -1.0f), c.rB);
            }
        }
    }

    // ===== 衝突イベントの組み立て =====

    // 2オブジェクトの組を表す1つの数値。小さいID・大きいIDの順に並べてから64bitに詰めるので、
    // どちらをa/bにしても同じ組は必ず同じ値になる（前フレームと突き合わせるための鍵）
    static uint64_t MakePairKey(uint32_t idA, uint32_t idB) {
        uint32_t lo = (std::min)(idA, idB);
        uint32_t hi = (std::max)(idA, idB);
        return ((uint64_t)lo << 32) | (uint64_t)hi;
    }

    // 接触点をすべて「組」単位に束ね、前ステップと突き合わせてEnter/Stay/Exitに振り分ける
    void EmitCollisionEvents() {
        events_.clear();

        std::unordered_map<uint64_t, PairInfo> current;
        current.reserve(contacts_.size() + triggerOverlaps_.size());

        // --- 押し返しのあった接触 ---
        for (const Contact& c : contacts_) {
            if (c.a->id == 0 || c.b->id == 0) { continue; } // 生成関数を通していない物は追跡できない
            PairInfo& info = current[MakePairKey(c.a->id, c.b->id)];
            AccumulatePair(info, *c.a, *c.b, c.point, c.normal, c.normalImpulse, c.approachSpeed, false);
        }

        // --- トリガーの重なり ---
        for (const TriggerOverlap& o : triggerOverlaps_) {
            if (o.a->id == 0 || o.b->id == 0) { continue; }
            PairInfo& info = current[MakePairKey(o.a->id, o.b->id)];
            AccumulatePair(info, *o.a, *o.b, o.point, o.normal, 0.0f, 0.0f, true);
        }

        // --- 今触れている組：前も触れていたならStay、今回からならEnter ---
        for (auto& [key, info] : current) {
            bool wasTouching = touchingPairs_.count(key) != 0;
            events_.push_back(MakeEvent(info, wasTouching ? CollisionPhase::Stay : CollisionPhase::Enter));
            pairHistory_[key] = info; // 離れたときに使うので最後の状態を控えておく
        }

        // --- 前は触れていたのに今は触れていない組：Exit ---
        for (uint64_t key : touchingPairs_) {
            if (current.count(key) != 0) { continue; }
            auto it = pairHistory_.find(key);
            if (it != pairHistory_.end()) {
                PairInfo info = it->second;
                // 控えてあるポインタは、その後オブジェクトが削除/移動されていると無効になっている。
                // IDから今のシーンを引き直し、見つからなければ「消えた物」としてイベントごと捨てる
                info.a = FindObjectById(info.idA);
                info.b = FindObjectById(info.idB);
                if (info.a && info.b) { events_.push_back(MakeEvent(info, CollisionPhase::Exit)); }
                pairHistory_.erase(it);
            }
        }

        // 次のステップと比べるために、今触れている組を控え直す
        touchingPairs_.clear();
        touchingPairs_.reserve(current.size());
        for (const auto& entry : current) { touchingPairs_.insert(entry.first); }
    }

    static void AccumulatePair(PairInfo& info, Object3D& a, Object3D& b, const Vector3& point,
                               const Vector3& normal, float impulse, float approachSpeed, bool isTrigger) {
        if (info.pointCount == 0) {
            info.a = &a;
            info.b = &b;
            info.idA = a.id;
            info.idB = b.id;
            info.isTrigger = isTrigger;
        }
        // 複数点で触れている場合、法線は「最も強く押し返した1点」のものを代表とする。
        // 平均を取ると、床と壁の角で同時に触れたときに、どちらでもない斜めの向きになってしまう。
        // 比べる相手は合計(impulse)ではなく1点あたりの最大(strongestImpulse)であることに注意。
        // 合計と比べると、2点目以降はほぼ必ず負けて最初の点に固定されてしまう
        if (info.pointCount == 0 || impulse > info.strongestImpulse) {
            info.normal = normal;
            info.approachSpeed = approachSpeed;
            info.strongestImpulse = impulse;
        }
        info.pointSum = Add(info.pointSum, point);
        info.pointCount++;
        info.impulse += impulse; // 撃力は合計。4点で支えている接地なら4点ぶんの合計が着地の強さになる
    }

    static CollisionEvent MakeEvent(const PairInfo& info, CollisionPhase phase) {
        CollisionEvent e{};
        e.phase = phase;
        e.a = info.a;
        e.b = info.b;
        e.isTrigger = info.isTrigger;
        e.point = (info.pointCount > 0) ? Scale(info.pointSum, 1.0f / (float)info.pointCount) : info.pointSum;
        e.normal = info.normal;
        e.impulse = info.impulse;
        e.approachSpeed = (phase == CollisionPhase::Enter) ? info.approachSpeed : 0.0f;
        return e;
    }

    // IDから今のシーン上のオブジェクトを探す。Exitイベントの相手を引き直すときにだけ使う（頻度は低い）
    Object3D* FindObjectById(uint32_t id) { return sceneObjects_ ? ::FindObjectById(*sceneObjects_, id) : nullptr; }

    // ===== 位置・姿勢の積分 =====
    void IntegratePositions(std::vector<Object3D>& objects, float deltaTime) {
        const float kMaxSpeed = 40.0f;        // 終端速度相当の頭打ち（数値が破綻して吹っ飛ぶのを防ぐ最後の砦）
        const float kMaxAngularSpeed = 30.0f; // rad/s

        for (auto& obj : objects) {
            if (obj.isStatic || !obj.usePhysics) { continue; }

            float speedSq = LengthSq(obj.velocity);
            if (speedSq > kMaxSpeed * kMaxSpeed) { obj.velocity = Scale(obj.velocity, kMaxSpeed / sqrtf(speedSq)); }
            float angSpeedSq = LengthSq(obj.angularVelocity);
            if (angSpeedSq > kMaxAngularSpeed * kMaxAngularSpeed) { obj.angularVelocity = Scale(obj.angularVelocity, kMaxAngularSpeed / sqrtf(angSpeedSq)); }

            // 剛体は重心まわりに回る。ところがtransform.translateが指すのは「ローカル原点」であって
            // 重心とは限らない（objモデルは形状の中心が原点からずれていることが多い）。
            // そこで「重心をどこへ持っていきたいか」を先に決め、姿勢を進めたあとで
            // 原点の位置を逆算する。こうしないと原点まわりに回ってしまい、撃力の計算（重心基準）と食い違う
            Vector3 localCenterOffset = obj.GetLocalCenterOffset();
            Vector3 targetCenterOfMass = Add(obj.GetCenterOfMass(), Scale(obj.velocity, deltaTime));

            obj.orientation = IntegrateQuaternion(obj.orientation, obj.angularVelocity, deltaTime);

            // 拘束された軸の姿勢を厳密に保つ。逆慣性を0にしてあるので原理的には回らないが、
            // 積分の丸め誤差が積もるとわずかにずれるため、ここで確実に打ち消しておく
            if (obj.rotationMode == RotationMode::PlanarZ || obj.rotationMode == RotationMode::UprightLock) {
                Vector3 euler = EulerXYZFromQuaternion(obj.orientation);
                if (obj.rotationMode == RotationMode::PlanarZ) { euler.x = 0.0f; euler.y = 0.0f; }
                else { euler.x = 0.0f; euler.z = 0.0f; }
                obj.orientation = QuaternionFromEulerXYZ(euler);
            }

            obj.transform.translate = Sub(targetCenterOfMass, RotateVector(obj.orientation, localCenterOffset));
        }
    }

    // ===== スリープ（静止判定）=====
    // 十分遅い状態が一定時間続いたら完全に止める。
    // 「今フレームの速度が小さいか」だけで判定すると、真上に投げた物が放物線の頂点で
    // 速度が一瞬0になった拍子に静止扱いされ、空中で回転まで止まってしまう
    void UpdateSleeping(std::vector<Object3D>& objects, float deltaTime) {
        const float kSleepLinearSpeed = 0.08f;  // m/s
        const float kSleepAngularSpeed = 0.20f; // rad/s
        const float kSleepDelay = 0.35f;        // 秒

        for (auto& obj : objects) {
            if (obj.isStatic || !obj.usePhysics) { obj.sleepTimer = 0.0f; continue; }

            bool slow = LengthSq(obj.velocity) < kSleepLinearSpeed * kSleepLinearSpeed &&
                        LengthSq(obj.angularVelocity) < kSleepAngularSpeed * kSleepAngularSpeed;
            if (!slow) { obj.sleepTimer = 0.0f; continue; }

            obj.sleepTimer += deltaTime;
            if (obj.sleepTimer >= kSleepDelay) {
                obj.velocity = { 0.0f, 0.0f, 0.0f };
                obj.angularVelocity = { 0.0f, 0.0f, 0.0f };
            }
        }
    }
};
