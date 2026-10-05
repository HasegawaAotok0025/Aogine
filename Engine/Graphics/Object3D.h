// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#pragma once
#include "GraphicsCommon.h"
#include "../Core/Quaternion.h"
#include "../Component/Component.h" // 後から挿す部品（コンポーネント）の土台
#include <type_traits>

// ============================================================================
// シーンに置く「1つのオブジェクト」を表すデータ型と、その生成関数をまとめたファイル。
// Object3Dは見た目（頂点/マテリアル/テクスチャ）と当たり判定・物理の設定を1つに束ねたもので、
// エンジン・ステージ・物理・プレイヤーのどこからも共通して扱う中心的なデータになる。
// ============================================================================

// UVTransform用のTranslate/Rotate/Scale（見た目上はSRTだが2D用途なのでZ成分はほぼ使わない）
struct UVTransform {
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    Vector3 rotate = { 0.0f, 0.0f, 0.0f };
    Vector3 translate = { 0.0f, 0.0f, 0.0f };
};

// 複数モデル描画対応：Sprite/Sphere/objモデルを問わず1つの描画対象として扱う汎用オブジェクト
// リソースはすべてComPtrで持つため、Object3Dが破棄される（vectorからeraseされる/スコープを抜ける）と
// 自動的にGPUリソースが解放される。明示的なRelease()は不要。
// 動くオブジェクト側の当たり判定形状（静止メッシュコライダーと衝突する際にどちらのアルゴリズムを使うか決める）
enum class ColliderShape { Box, Sphere };

// 回転の拘束（Unityの Rigidbody > Constraints > Freeze Rotation に相当）
//   Free        : 3軸とも自由に回る。箱・球・瓦礫など「転がってよいもの」向けの既定値
//   PlanarZ     : 画面奥行き（Z軸）まわりだけ回る。奥行き1マスの2Dステージ用。
//                 横から見た画面の中で前転・後転はするが、画面の手前/奥へ倒れることはない。
//                 横スクロールのゲームで「2Dのまま宙返りさせたい」ものはこれを使う
//   UprightLock : 上下軸（Y＝ヨー）まわりだけ回り、前後左右の傾き（ピッチ／ロール）は常に0に保つ。
//                 立って歩くキャラクター向け。「転ばないが向きは変えられる」状態
//   Frozen      : 一切回らない。回転させたくない仕掛けやUI的なオブジェクト向け
//
// この拘束は「回った後で角度を0に戻す」のではなく、拘束された軸の慣性モーメントの逆数を0にして
// 実現している（＝その軸まわりには無限の慣性があり、どんな撃力を受けても回らない）。
// 衝突の応答計算そのものに拘束が入るので、押し出しと回転が食い違わない
enum class RotationMode { Free, PlanarZ, UprightLock, Frozen };

// オブジェクトを一意に指す番号を配る。生成のたびに1つ増える通し番号で、二度と再利用しない。
//
// std::vectorの添字は、途中の要素が消えると後ろが全部ずれてしまうため「同じ物を指し続ける名札」には使えない。
// ポインタも、vectorが拡張されると全要素が別の場所へ移るので同じ理由で使えない。
// そこで数値の名札を持たせておく。衝突イベントで「前のフレームも触れていた組か」を判定するには
// フレームをまたいで同じ物だと分かる手がかりが要る、というのが直接の動機
inline uint32_t NextObjectId() {
    static uint32_t counter = 0;
    return ++counter; // 0は「無効」の意味に取っておく
}

// このオブジェクトが何から作られたか。シーンをファイルへ保存して読み直すとき、
// 同じ物をもう一度作るために「元が何だったか」が要る。
// 頂点データを見て後から当てることはできない（立方体と読み込んだモデルの区別がつかない）ので、
// 作った時点で控えておく
enum class ObjectSource { Sprite, Sphere, Cube, Model };

struct Object3D {
    // 生成時に配られる一意な番号（0は無効）。Object3Dをコピーすると番号も複製されてしまうが、
    // 生成関数を通す限り1オブジェクト＝1番号になる
    uint32_t id = 0;

    ObjectSource source = ObjectSource::Cube;
    std::string sourceModel; // source==Modelのとき "resources/SuzannuObj/suzannu.obj" の形式で入る
    std::string name;               // 名前（ログやFindで使う）

    // 種類を表す短い名前（Unityの Tag と同じ考え方）。"Player" "Goal" "Ground" など。
    // nameは1体ずつ違う（"Goal_3_5"）ため衝突相手の判別には使えないが、tagは同じ種類なら同じ値になる。
    // 衝突イベントで「ゴールに触れたか」を調べるのはこれを見て行う
    std::string tag;
    bool isSprite = false;          // true:Sprite(平行投影) / false:Sphere/objモデル(透視投影)
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ComPtr<ID3D12Resource> indexResource; // IndexResource（頂点インデックス）
    D3D12_INDEX_BUFFER_VIEW ibv{};        // IndexBufferView（IBV）
    UINT indexCount = 0;                  // DrawIndexedInstancedで使うIndex数

    ComPtr<ID3D12Resource> transformResource;
    TransformationMatrix* transformData = nullptr;
    Transform transform{};

    AABB localAABB; // ボックスコライダー（ローカル空間）。頂点データから自動計算される

    // ===== 簡易物理（重力・減衰・当たり判定） =====
    // 「重力」だけでなく速度の減衰・衝突による押し出し・転がり演出まで含めた物理シミュレーション全体のON/OFFなので、
    // 単なる重力フラグではなくusePhysicsという名前にしている。falseにすると毎フレーム velocity は強制的に0へ戻され、
    // 古い運動エネルギーを一切持ち越さない（オフ→オン時にいきなり過去の速度で動き出すのを防ぐ）
    bool usePhysics = false;      // trueにすると毎フレーム重力を受けて落下し、減衰・衝突応答も働くようになる
    bool isStatic = false;        // trueだと物理の影響を受けず動かない（地面や壁など）
    Vector3 velocity{ 0.0f, 0.0f, 0.0f }; // 重心の速度（m/s）
    Vector3 angularVelocity{ 0.0f, 0.0f, 0.0f }; // 角速度（rad/s、ワールド基準の回転軸ベクトル）。向きが回転軸、長さが回る速さ
    ColliderShape colliderShape = ColliderShape::Box; // このオブジェクトが「動く側」のときの衝突形状
    // 描くかどうか。当たり判定だけを残して見た目を別に用意したいときにfalseにする
    bool visible = true;

    // ===== 剛体としての性質 =====
    // 質量[kg]。衝突の応答はこの質量と、下の形状から計算される慣性モーメントで決まる。
    // 重い物ほど押されにくく、細長い物ほど長手方向には回りにくい、という当たり前の挙動がこれで出る
    float mass = 1.0f;

    // 反発係数（0.0=まったく跳ねない、1.0=完全弾性衝突で高さが落ちない）。
    // 実際の物体は1未満で、跳ねるたびにエネルギーを失う
    float restitution = 0.05f;

    // 摩擦係数μ（クーロン摩擦）。接触面に沿って働く力の上限が「μ×垂直抗力」になる。
    // 0.0=つるつるの氷、0.6程度=ゴムや木材。速度を何割か削るような近似ではなく、
    // 垂直抗力に比例するので「強く押しつけられているほど滑りにくい」が正しく再現される
    float friction = 0.5f;

    // 回転の拘束（Unityの Rigidbody > Constraints > Freeze Rotation 相当）
    RotationMode rotationMode = RotationMode::Free;

    // ===== 姿勢（回転）の実体 =====
    // 物理計算はこのクォータニオンを積分して進める。オイラー角(transform.rotate)は
    // 表示・編集用の見せかけで、毎ステップここから作り直される。
    // 逆に外部からtransform.rotateが書き換えられた場合は、そのフレームでクォータニオン側へ取り込む
    // （syncedEulerと比べて、変わっていれば「誰かが編集した」と判断する）
    Quaternion orientation = QuaternionIdentity();
    Vector3 syncedEuler{ 0.0f, 0.0f, 0.0f };

    // 速度がほぼ0の状態が続いた時間[秒]。一定時間続いたら「眠らせて」完全に静止させる。
    // 「今フレームの速度が小さいか」ではなく時間で判定するのが要点で、そうしないと
    // 真上に投げた物が放物線の頂点（速度が一瞬0になる）で静止扱いされてしまう
    float sleepTimer = 0.0f;

    // このフレーム、ほぼ真上向きの法線を持つ相手（床など）に接地しているか。UpdatePhysicsが衝突解決のたびに更新する。
    // ジャンプ操作などで「地面に着いているか」を厳密に判定するために使う（速度がほぼ0、という曖昧な判定だと
    // ジャンプの頂点付近でも条件を満たしてしまい、空中で連続ジャンプできてしまうため）
    bool isGrounded = false;

    // ===== ボックスコライダー（OBB）のON/OFF =====
    // falseにすると、このオブジェクトはOBB(SAT)による当たり判定に一切参加しなくなる（押す側にも押される側にもならない）
    // また、当たり判定の枠（SetShowColliders）も出なくなる
    bool useBoxCollider = true;

    // ===== トリガー（重なりを知らせるだけで、押し返さない領域） =====
    // Unityの Collider > Is Trigger と同じ考え方。trueにすると衝突の解決（押し出し・反発・摩擦）から外れ、
    // 代わりに「重なった／重なり続けている／離れた」がCollisionEventとしてゲーム側へ届くようになる。
    // ゴール地点・チェックポイント・ダメージ床・アイテムのように「触れたことを知りたいだけ」の物に使う。
    // 押し返さないので、通り抜けながら判定できる
    bool isTrigger = false;

    // ===== メッシュコライダー（静止オブジェクト側で使う。Unityと同じくメッシュコライダーは静止専用） =====
    // trueにすると、isStatic前提でこのオブジェクトの実メッシュ（三角形単位）を衝突相手として使う
    // （Sphereコライダーを持つ動くオブジェクトのみ対応。Boxコライダー同士は従来通りOBBで判定する）
    // 判定は毎フレーム全三角形を総当たりすると重いため、まずOBB（ボックスコライダー）でおおまかに判定し、
    // 「当たる可能性がある」ときだけ三角形単位の精密な判定に進む（Engine::UpdatePhysics参照）
    bool useMeshCollider = false;
    std::vector<Vector3> localPositions;   // メッシュコライダー用のローカル頂点位置
    std::vector<uint32_t> triangleIndices; // メッシュコライダー用の三角形インデックス
    MeshLineData meshLineData;             // メッシュコライダーのワイヤーフレーム表示用（初回描画時に遅延構築する）

    // 複雑な形状同士（スザンヌ対スザンヌなど）の当たり判定で使う、頂点1つあたりの当たり半径（スキン幅）
    // このオブジェクトが「動く側」で、かつcolliderShapeがBox（＝球ではない複雑な形状）のときにのみ使われる。
    // 大きくするとめり込みにくくなる代わりに、実際の表面より少し手前で止まって見えるようになる
    float meshVertexRadius = 0.15f;

    // 現在のtransformを反映したワールド空間のボックスコライダー（OBB）を求める（毎フレーム呼んでよい軽い計算）
    // Unityと同じく、オブジェクトを回転させるとコライダーもそのまま追従して回転する
    OBB GetWorldOBB() const {
        Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
        return TransformToOBB(localAABB, worldMatrix);
    }

    // colliderShape==Sphereのオブジェクト用：ワールド空間での半径を求める（scaleは等倍を想定）
    float GetSphereRadius() const {
        return (localAABB.max.x - localAABB.min.x) * 0.5f * transform.scale.x;
    }

    // ===== 剛体計算に必要な量 =====

    // 重心のワールド座標。剛体は必ず重心を中心に回るので、撃力の腕の長さ（r）はここから測る。
    // transform.translateは「ローカル原点の位置」であって重心とは限らない（objモデルは中心がずれていることが多い）ため、
    // 形状の中心＝重心とみなしてOBBの中心を使う
    Vector3 GetCenterOfMass() const { return GetWorldOBB().center; }

    // ローカル原点から重心へのベクトル（スケール適用済み・回転前）。
    // 姿勢が変われば重心の位置も原点から見て動くので、「重心を固定したまま回す」ために必要になる
    Vector3 GetLocalCenterOffset() const {
        return {
            (localAABB.min.x + localAABB.max.x) * 0.5f * transform.scale.x,
            (localAABB.min.y + localAABB.max.y) * 0.5f * transform.scale.y,
            (localAABB.min.z + localAABB.max.z) * 0.5f * transform.scale.z,
        };
    }

    // 質量の逆数。静止オブジェクトは「無限に重い」とみなして0を返す（どんな撃力でも動かない）
    float GetInverseMass() const {
        if (isStatic || !usePhysics || mass <= 0.0f) { return 0.0f; }
        return 1.0f / mass;
    }

    // 慣性モーメントの逆数（ローカル軸ごと）。回転拘束で固定された軸は0にする（＝その軸には回らない）。
    //   直方体：I = (m/3)(h1^2 + h2^2)  ※hは半径（中心から面までの距離）
    //   球    ：I = (2/5) m r^2
    // 細長い形状ほど長手方向まわりには回りやすく、横倒し方向には回りにくい、が自然に出る
    Vector3 GetInverseInertiaLocal() const {
        if (isStatic || !usePhysics || mass <= 0.0f || rotationMode == RotationMode::Frozen) {
            return { 0.0f, 0.0f, 0.0f };
        }
        Vector3 inertia{};
        if (colliderShape == ColliderShape::Sphere) {
            float r = GetSphereRadius();
            float i = 0.4f * mass * r * r;
            inertia = { i, i, i };
        } else {
            Vector3 h = GetWorldOBB().halfSize;
            inertia = {
                mass * (h.y * h.y + h.z * h.z) / 3.0f,
                mass * (h.x * h.x + h.z * h.z) / 3.0f,
                mass * (h.x * h.x + h.y * h.y) / 3.0f,
            };
        }

        Vector3 invInertia{
            (inertia.x > 1e-8f) ? 1.0f / inertia.x : 0.0f,
            (inertia.y > 1e-8f) ? 1.0f / inertia.y : 0.0f,
            (inertia.z > 1e-8f) ? 1.0f / inertia.z : 0.0f,
        };

        // 拘束された軸は逆数を0に＝その軸まわりの慣性が無限大。撃力を受けても回らない
        if (rotationMode == RotationMode::PlanarZ) { invInertia.x = 0.0f; invInertia.y = 0.0f; }
        else if (rotationMode == RotationMode::UprightLock) { invInertia.x = 0.0f; invInertia.z = 0.0f; }
        return invInertia;
    }

    ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;
    UVTransform uvTransform{};
    int lightingMode = 0; // LightingModeと対応（int で保持）
    // 色の合成方法。kNone以外にすると、materialData->color.w（α）が効くようになる。
    // 半透明の物は奥行きを書き込まず、不透明な物を描いたあとに遠い順で描かれる
    BlendMode blendMode = BlendMode::kNone;
    bool useChangeTexture = false; // false:uvChecker.png / true:changeResource.png（Sprite/Sphere用の簡易切り替え）
    // テクスチャを貼らず、下のcolorだけで描くか。
    // 作る物（ステージのブロック・キャラクター・仕掛け）はこちら側で、
    // uvChecker.pngはUVの並びを確かめるための格子模様なので、貼ったままだと
    // 画面じゅうが虹色の格子と座標の数字で埋まる。
    // 格子を見たいときだけ false にする
    bool untextured = true;

    // objファイルから読み込んだモデル専用のテクスチャ（mtlのmap_Kdから読み込む）。
    // 持っている場合はuseChangeTextureの2択ではなく、こちらを優先して使用する
    bool hasOwnTexture = false;
    ComPtr<ID3D12Resource> textureResource;
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU{};

    // ===== 後から挿した部品（コンポーネント）=====
    // 「このオブジェクトだけの振る舞い」をここへ足す。回る・揺れる・光る・破片を出す……。
    //
    // これより上の項目は、全オブジェクトが必ず持つもの（位置・見た目・当たり判定）。
    // 一部の物にしか要らない機能をそこへ足していくと、使わない物にも項目が付いて回るので、
    // そういうものは部品にして、必要な物にだけ挿す。詳しくはEngine/Component/Component.h。
    //
    // コピーすると中身まで複製される（面倒はComponentListが見ている）ので、
    // Ctrl+Dの複製もvectorの入れ替えも、今まで通りに書ける
    ComponentList components;

    // 部品を付ける／探す短縮形。components.Add<T>() と同じだが、
    // Unityと同じ綴りで書けるようにしてある（obj.AddComponent<SpinComponent>()）
    template <class T, class... Args> T* AddComponent(Args&&... args) { return components.Add<T>(std::forward<Args>(args)...); }
    template <class T> T* GetComponent() { return components.Get<T>(); }
    template <class T> const T* GetComponent() const { return components.Get<T>(); }
    template <class T> bool HasComponent() const { return components.Has<T>(); }
};


// Object3Dは「投げないムーブ」ができること。
//
// objects_ は std::vector<Object3D> なので、1つ増えるたびに全要素が引っ越すことがある。
// このときvectorは、ムーブがnoexceptなら移すだけで済ませ、そうでなければ全部をコピーする。
// コピーになると、オブジェクト1つにつき
//   ・ComPtrの参照カウントを4本ぶん上げ下げする
//   ・付いている部品を1つずつ作り直す（ComponentList::CopyFrom）
// が走る。数百個のシーンでは、置いた瞬間に目に見えて詰まる。
//
// さらに厄介なのは、ムーブなら部品の実体（unique_ptrの中身）は動かないのに、
// コピーだと作り直されるため、部品を指していたポインタが静かに古いものを指し続けること。
// どちらになるかは「誰かがObject3Dへ足したメンバ次第」で変わるので、
// 変わった瞬間にビルドを止める。実行して気づける類の違いではない
static_assert(std::is_nothrow_move_constructible_v<Object3D>,
              "Object3Dへ、ムーブがnoexceptでないメンバが入りました。"
              "vectorの引っ越しがコピーになり、遅くなるうえ部品の番地が変わります");
// Object3Dとして配置できる基本形状の種類
enum class PrimitiveKind { Sprite, Sphere, Cube };

// Object3Dの生成（頂点バッファ・定数バッファの確保と初期値設定を行う）
inline Object3D CreateObject3D(ID3D12Device* device, PrimitiveKind kind) {
    Object3D obj{};
    obj.id = NextObjectId();
    obj.isSprite = (kind == PrimitiveKind::Sprite);

    MeshBuffers mesh;
    switch (kind) {
    case PrimitiveKind::Sprite: obj.name = "Sprite"; obj.source = ObjectSource::Sprite; mesh = CreateSpriteMeshBuffers(device); break;
    case PrimitiveKind::Sphere: obj.name = "Sphere"; obj.source = ObjectSource::Sphere; mesh = CreateSphereMeshBuffers(device); break;
    case PrimitiveKind::Cube:   obj.name = "Cube";   obj.source = ObjectSource::Cube;   mesh = CreateCubeMeshBuffers(device, 2.0f); break;
    }
    obj.vertexResource = mesh.vertexResource;
    obj.vbv = mesh.vbv;
    obj.indexResource = mesh.indexResource;
    obj.ibv = mesh.ibv;
    obj.indexCount = mesh.indexCount;
    obj.localAABB = mesh.localAABB;
    obj.localPositions = mesh.localPositions;
    obj.triangleIndices = mesh.triangleIndices;
    obj.colliderShape = (kind == PrimitiveKind::Sphere) ? ColliderShape::Sphere : ColliderShape::Box;

    if (kind == PrimitiveKind::Sprite) {
        obj.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
        obj.lightingMode = (int)LightingMode::kNone; // 2DはLightingしない運用がデフォルト
    }
    else {
        // 置いた直後は原点。位置はゲームのコードが決める
        obj.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
        obj.lightingMode = (int)LightingMode::kHalfLambert; // 3DはHalfLambertをデフォルトに
    }

    obj.transformResource = CreateBufferResource(device, sizeof(TransformationMatrix));
    obj.transformResource->Map(0, nullptr, reinterpret_cast<void**>(&obj.transformData));
    obj.transformData->WVP = MakeIdentity4x4();
    obj.transformData->World = MakeIdentity4x4();

    obj.materialResource = CreateBufferResource(device, sizeof(Material));
    obj.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&obj.materialData));
    obj.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    obj.materialData->lightingMode = obj.lightingMode;
    obj.materialData->uvTransform = MakeIdentity4x4();

    return obj;
}

// 読み込み済みobjモデル1つぶんのGPUリソース（頂点/インデックスバッファ・テクスチャ）
// ディレクトリ+ファイル名をキーにEngine側でキャッシュしておくことで、同じモデルを2体目以降置くときに
// ファイルの再パース・頂点バッファの再アップロード・テクスチャの再アップロードを省略できる
// （Sphere/CubeのようにCreate一発で追加インスタンスを置けるようにするための土台）
struct CachedModelData {
    MeshBuffers mesh;
    bool hasOwnTexture = false;
    ComPtr<ID3D12Resource> textureResource;
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU{};
};

// objファイル（＋mtl・テクスチャ）を読み込み、GPUリソース一式をCachedModelDataとして構築する
// 「頂点/マテリアル/テクスチャの読み込み〜GPUリソース構築」をすべてこの関数内で完結させてあるので、
// 呼び出し側はディレクトリとファイル名を渡すだけでよい
//
// srvIndex/pendingIntermediateResourcesについて：
//   ・srvIndexはモデル専用テクスチャを登録するSRVディスクリプタヒープのスロット番号。呼び出し側でインクリメントして使い切りを管理すること
//   ・テクスチャのアップロードにはGPUコマンドが必要なため、生成中のcommandListにコマンドを積む。
//     アップロード用の中間リソースはGPU転送完了まで解放できないため、pendingIntermediateResourcesに積んでおき、
//     呼び出し側でコマンド実行＆フェンス待機した後に解放すること
//   ・allowOwnTexture=falseを渡すと、mtlにmap_Kdがあってもモデル専用テクスチャを読まない。
//     SRVディスクリプタヒープに空きが無いときに、呼び出し側がヒープの外への書き込みを避けるために使う
// 部品1つぶんのキャッシュ。nameはobjのo/gで付いていた名前で、キャッシュの見出しに使う
struct CachedModelPart {
    std::string name;
    CachedModelData data;
};

// objファイルを読み込み、o/gで区切られた部品ごとにGPUリソースを構築する。
// 部品が1つも無いファイル（区切りを書いていない普通のobj）でも、必ず1つは返る。
//
// テクスチャは1つのobjにつき1回だけ読み、全部品で共有する。
// 部品ごとに読み直すとSRVディスクリプタヒープの枠を部品の数だけ消費してしまい、
// 「分割したせいで置けるモデルが減る」ことになる
inline std::vector<CachedModelPart> LoadCachedModelParts(
    ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
    ID3D12DescriptorHeap* srvDescriptorHeap, UINT descriptorSizeSRV, UINT srvIndex,
    std::vector<ComPtr<ID3D12Resource>>& pendingIntermediateResources,
    const std::string& directoryPath, const std::string& filename, bool allowOwnTexture = true)
{
    ModelData modelData = LoadObjFile(directoryPath, filename);

    if (modelData.vertices.empty() || modelData.parts.empty()) {
        // objファイルが見つからなかった/読み込めなかった場合は、見た目で分かるデバッグ用の立方体で代用する
        // （何も描画されない・落ちるより、ここで気づける方が親切なため）
        Log("[LoadCachedModelParts] \"" + directoryPath + "/" + filename + "\" を読めませんでした。代わりに立方体を置きます");
        CachedModelData cached{};
        cached.mesh = CreateCubeMeshBuffers(device, 2.0f);
        return { { "", std::move(cached) } };
    }

    // mtlにmap_Kdの指定があれば、モデル専用のテクスチャとして読み込む。
    //
    // 指定されたファイルが無いときは、読まずに色だけで描く。
    // LoadTextureは見つからないとマゼンタの市松模様で代用するので、
    // 「まだ絵を描いていないだけ」のモデルが画面で派手に化けてしまう。
    // mtlに先に行き先を書いておいて、あとからpngを置くだけで絵が乗る、
    // という進め方ができるようにするための逃げ道でもある
    bool hasOwnTexture = false;
    ComPtr<ID3D12Resource> textureResource;
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU{};
    if (!modelData.material.textureFilePath.empty() && allowOwnTexture) {
        std::error_code textureError;
        if (std::filesystem::exists(modelData.material.textureFilePath, textureError)) {
            textureSrvHandleGPU = LoadTextureToSRV(
                device, commandList, srvDescriptorHeap, descriptorSizeSRV, srvIndex,
                modelData.material.textureFilePath, textureResource, pendingIntermediateResources);
            hasOwnTexture = true;
        } else {
            Log("[LoadCachedModelParts] mtlが指しているテクスチャがまだありません: \"" +
                modelData.material.textureFilePath + "\"（色だけで描きます）");
        }
    }

    std::vector<CachedModelPart> parts;
    parts.reserve(modelData.parts.size());
    for (const ModelPart& part : modelData.parts) {
        CachedModelData cached{};
        // 部品が1つしか無いなら、ファイル全体をそのまま1つのメッシュにする（従来と同じ扱い）
        cached.mesh = (modelData.parts.size() == 1)
            ? CreateMeshBuffersFromModelData(device, modelData)
            : CreateMeshBuffersFromModelPart(device, modelData, part);
        cached.hasOwnTexture = hasOwnTexture;
        cached.textureResource = textureResource;       // ComPtrなので、全部品で1つのリソースを共有する
        cached.textureSrvHandleGPU = textureSrvHandleGPU;
        parts.push_back({ (modelData.parts.size() == 1) ? std::string() : part.name, std::move(cached) });
    }
    if (parts.size() > 1) {
        Log(std::format("[LoadCachedModelParts] \"{}/{}\" は{}個の部品に分かれています", directoryPath, filename, parts.size()));
    }
    return parts;
}

// キャッシュ済みのCachedModelDataから、新しいObject3D（1体分のTransform/Material付き）を生成する
// 頂点/インデックスバッファとテクスチャはキャッシュのものをそのまま共有し（ComPtrなので参照カウントで解放管理される）、
// Transform/Materialだけを1体ごとに新規作成する。SphereやCubeをCreateボタンで増やすのと同じ感覚で
// 同じモデルの追加インスタンスを安く置けるようにするためのもの
inline Object3D CreateObject3DFromCachedModel(ID3D12Device* device, const CachedModelData& cached, const std::string& name) {
    Object3D obj{};
    obj.id = NextObjectId();
    obj.isSprite = false;
    obj.name = name;
    obj.source = ObjectSource::Model;

    obj.vertexResource = cached.mesh.vertexResource;
    obj.vbv = cached.mesh.vbv;
    obj.indexResource = cached.mesh.indexResource;
    obj.ibv = cached.mesh.ibv;
    obj.indexCount = cached.mesh.indexCount;
    obj.localAABB = cached.mesh.localAABB;
    obj.localPositions = cached.mesh.localPositions;
    obj.triangleIndices = cached.mesh.triangleIndices;

    obj.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} }; // 置いた直後は原点
    obj.lightingMode = (int)LightingMode::kHalfLambert;

    obj.transformResource = CreateBufferResource(device, sizeof(TransformationMatrix));
    obj.transformResource->Map(0, nullptr, reinterpret_cast<void**>(&obj.transformData));
    obj.transformData->WVP = MakeIdentity4x4();
    obj.transformData->World = MakeIdentity4x4();

    obj.materialResource = CreateBufferResource(device, sizeof(Material));
    obj.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&obj.materialData));
    obj.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    obj.materialData->lightingMode = obj.lightingMode;
    obj.materialData->uvTransform = MakeIdentity4x4();

    obj.hasOwnTexture = cached.hasOwnTexture;
    obj.textureResource = cached.textureResource;
    obj.textureSrvHandleGPU = cached.textureSrvHandleGPU;

    return obj;
}
