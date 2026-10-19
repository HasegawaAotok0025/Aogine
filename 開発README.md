# 開発README（リファレンス）

ゲームのコードから使う関数の一覧。すべて `#include "../Engine/Engine.h"` で使える。
関数の細かい説明は、各ヘッダのコメントに書いてある。

- [1. 全体の流れ](#1-全体の流れ)
- [2. 入口（App.h）](#2-入口apph)
- [3. Engine](#3-engine)
- [4. Object3D（置いた物）](#4-object3d置いた物)
- [5. Camera（3Dのカメラ）](#5-camera3dのカメラ)
- [6. Input（キーボード・マウス・パッド）](#6-inputキーボードマウスパッド)
- [7. 音（SoundBank）](#7-音soundbank)
- [8. 物理と衝突](#8-物理と衝突)
- [9. 画面に出す（文字・2D）](#9-画面に出す文字2d)
- [10. 部品（Component）](#10-部品component)
- [11. 数学](#11-数学)
- [12. 気をつけること](#12-気をつけること)
- [13. デバッグビュー（F2）](#13-デバッグビューf2)

---

## 1. 全体の流れ

```
main.cpp
 ├ game.GetStartup()      … 窓の題・全画面かどうか（Game/Game.cpp）
 ├ engine.Initialize()
 ├ game.Initialize(engine) … 最初に1回。物を置き、毎フレームの処理（Game::Update）を登録する
 ├ engine.Run()           … 窓が閉じるまで毎フレーム：
 │    入力 → 物理(1/60秒刻み・衝突を通知) → 部品の更新 → Game::Update（SetUpdateCallback）
 │    → Destroyの実行 → カメラ確定 → 描画
 ├ game.Finalize()        … 終わる直前に1回。ゲームが作った物を片付ける
 └ engine.Shutdown()
```

座標系：**Y が上**。長さの単位はメートルのつもりで扱う（重力 -9.8）。角度はラジアン。

---

## 2. 入口（Game クラス）

ゲームは `Game/Game.h` の `Game` クラスに書く。`main.cpp` が作り、エンジンの前後で呼ぶ。

| 関数 | 役目 |
|---|---|
| `AppStartup GetStartup() const` | 起動設定を返す（エンジンを作る前に1回） |
| `void Initialize(Engine& engine)` | 起動直後に1回。物を置き、`SetUpdateCallback` で `Update` を登録する |
| `void Update(float deltaTime)` | 毎フレーム。物理・部品の更新の後、描画の前 |
| `void Finalize()` | 終わる直前に1回。ゲームが自分で作った物を片付ける |

`AppStartup` の項目：

| 項目 | 既定 | 意味 |
|---|---|---|
| `windowTitle` | `L"Aogine"` | 窓の題 |
| `fullscreen` | `false` | 全画面で始めるか。起動引数 `-window` で必ず窓(1280x720)になる |
| `convertToSrgbOnOutput` | `true` | 普通は触らない |

---

## 3. Engine

### 物を置く

| 関数 | 戻り値 | 説明 |
|---|---|---|
| `AddCube()` | `Object3D&` | 立方体（1辺2、中心が原点） |
| `AddSphere()` | `Object3D&` | 球 |
| `AddSprite()` | `Object3D&` | 1280x720の板の上に置く板（2Dの `Sprite2D` の方が扱いやすい） |
| `AddModel("resources/フォルダ", "ファイル.obj")` | `Object3D&` | OBJを読んで置く。2回目以降は読み込み済みを使い回す |
| `AddModelParts(dir, file)` | `std::vector<uint32_t>` | OBJが複数の部品(o/g)に分かれているとき、全部品を置いてidを返す |
| `SetModel(id, dir, file)` | `bool` | 置いてある物の形を別のOBJに差し替える（位置・色・部品・物理はそのまま。読み込みは初回だけ） |

> 置いた直後は原点・大きさ1・回転0。立方体は1辺2、球は半径1。
>
> 戻り値の `Object3D&` は**その場でだけ**使う。後で使うなら `.id` を控えて `FindById` で引き直す（→ [12](#12-気をつけること)）。

### 探す・消す

| 関数 | 説明 |
|---|---|
| `FindById(id)` | idで探す。無ければ `nullptr` |
| `FindByName(name)` / `FindByTag(tag)` | 名前・タグで最初の1体 |
| `FindAllByTag(tag)` | タグが一致するもの全部のid |
| `Objects()` | 置いてある物の一覧（`std::vector<Object3D>&`） |
| `Destroy(id)` | 消す。**フレームの終わりにまとめて消える**ので、衝突の通知の中から呼んでも安全 |
| `DestroyAll()` | 全部消す（場面の切り替えに） |

### 毎フレームの処理・通知

| 関数 | 説明 |
|---|---|
| `SetUpdateCallback([&](float dt){ ... })` | 毎フレーム呼ばれる。`dt` は実時間[秒] |
| `SetCollisionCallback([&](const CollisionEvent& e){ ... })` | 何かが触れたとき（→ [8](#8-物理と衝突)） |

### 時間

| 関数 | 説明 |
|---|---|
| `SetTimeScale(s)` / `TimeScale()` | 物理と部品にかける倍率。`0`でポーズ、`0.5`でスロー。`SetUpdateCallback`の`dt`には影響しない |
| `DeltaTime()` | 直前のフレームの経過[秒] |
| `TotalTime()` | 起動からの通算[秒] |

### 見た目

| 関数 | 説明 |
|---|---|
| `SetClearColor({r,g,b,a})` | 背景色 |
| `SetShadows(bool)` | 地面に落ちる影（既定ON） |
| `SetLightDirection({x,y,z})` / `SetLightColor({r,g,b,a})` / `SetLightIntensity(f)` | 平行光源 |
| `SetShowColliders(bool)` | 当たり判定の枠を線で出す（デバッグ用） |

### カメラ

| 関数 | 説明 |
|---|---|
| `GetCamera()` | 3Dのカメラ（→ [5](#5-camera3dのカメラ)） |
| `SetCamera2D(縦に映す長さ)` | 平行投影にする（2Dゲーム向け） |
| `SetCamera3D()` | 透視投影に戻す |

### マウスで指す

| 関数 | 説明 |
|---|---|
| `MouseRay(origin, dir)` | 今のマウス位置からのレイ。窓の外なら `false` |
| `ScreenPointToRay(px, py, origin, dir)` | 画面のピクセル位置からのレイ |
| `Raycast(origin, dir, 最大距離=1000, タグ="")` | 一番手前に当たった物。`RaycastHit{ object, distance, point }` を返す（当たらなければ `object == nullptr`） |

```cpp
Vector3 o, d;
if (engine.Input().IsMouseTriggered(MouseButton::Left) && engine.MouseRay(o, d)) {
    RaycastHit hit = engine.Raycast(o, d);
    if (hit.object) { engine.Destroy(hit.object->id); }
}
```

### そのほか

| 関数 | 説明 |
|---|---|
| `Input()` | 入力（→ [6](#6-inputキーボードマウスパッド)） |
| `Sounds()` | 音（→ [7](#7-音soundbank)） |
| `Audio()` | 音の低レベルAPI（普通は `Sounds()` を使う） |
| `Physics()` | 物理の設定（→ [8](#8-物理と衝突)） |
| `RequestScreenshot()` | 画面を `screenshots/` にpngで保存。**F12**でも撮れる（`SetScreenshotKeyEnabled(false)`で無効） |
| `Quit()` | ゲームを終わる |
| `WindowPixelWidth()` / `WindowPixelHeight()` | 実際の窓の大きさ[px] |

---

## 4. Object3D（置いた物）

よく使う項目：

| 項目 | 型 | 説明 |
|---|---|---|
| `id` | `uint32_t` | 変わらない番号（0は無効） |
| `name` / `tag` | `std::string` | 名前 / 種類（衝突相手の見分けに使う） |
| `transform.translate` | `Vector3` | 位置 |
| `transform.rotate` | `Vector3` | 回転（オイラー角[rad]） |
| `transform.scale` | `Vector3` | 大きさ |
| `visible` | `bool` | 描くか |
| `materialData->color` | `Vector4` | 色（テクスチャと掛け算） |
| `untextured` | `bool` | `true`で色だけ（テクスチャを貼らない） |
| `lightingMode` | `int` | 陰影 `(int)LightingMode::kNone / kLambert / kHalfLambert` |
| `uvTransform` | `UVTransform` | テクスチャの位置・拡縮・回転 |

物理の項目：

| 項目 | 既定 | 説明 |
|---|---|---|
| `usePhysics` | `false` | 重力を受けて動く |
| `isStatic` | `false` | 動かない（床・壁） |
| `velocity` / `angularVelocity` | 0 | 速度[m/s] / 角速度[rad/s] |
| `mass` / `friction` / `restitution` | 1 / 0.5 / 0.05 | 質量・摩擦・反発 |
| `useBoxCollider` | `true` | 箱の当たり判定 |
| `colliderShape` | `Box` | 動く側の形 `ColliderShape::Box / Sphere` |
| `useMeshCollider` | `false` | メッシュの当たり判定（動かない物だけ） |
| `isTrigger` | `false` | 押し返さず、触れたことだけ通知する（ゴール・アイテム） |
| `rotationMode` | `Free` | `Free` / `PlanarZ`(2D) / `UprightLock`(転ばない) / `Frozen`(回らない) |
| `isGrounded` | — | 何かの上に乗っているか（読むだけ） |

部品：`AddComponent<T>()` / `GetComponent<T>()` / `HasComponent<T>()`（→ [10](#10-部品component)）

---

## 5. Camera（3Dのカメラ）

`Camera& cam = engine.GetCamera();`

| 関数 | 説明 |
|---|---|
| `SetPosition(pos)` / `GetPosition()` | 位置 |
| `SetRotation(euler)` / `GetRotation()` | 向き（x=上下・正で下向き、y=左右・正で右向き） |
| `LookAt(target)` | targetの方を向く |
| `Forward()` / `Right()` / `Up()` | カメラの向き（長さ1） |
| `SetFovY(rad)` / `GetFovY()` | 画角（既定 0.45） |
| `SetNearFar(near, far)` | 描く距離の範囲（既定 0.1〜100） |
| `SetProjection(CameraProjection::Perspective / Orthographic)` | 透視 / 平行 |
| `SetOrthoHeight(m)` | 平行投影で縦に映す長さ |
| `SetFreeMove(bool)` | **デバッグ用**の自由移動（右ドラッグ・WASD・E/Q・Shift） |
| `GetViewProjectionMatrix()` | 描画に使う行列 |

---

## 6. Input（キーボード・マウス・パッド）

`InputEngine& in = engine.Input();` キーは `DIK_W` `DIK_SPACE` `DIK_ESCAPE` などのDirectInputのコード。

| 関数 | 説明 |
|---|---|
| `IsKeyDown(DIK_x)` | 押している間ずっと |
| `IsKeyTriggered(DIK_x)` | 押した瞬間の1フレーム |
| `IsKeyReleased(DIK_x)` | 離した瞬間の1フレーム |
| `IsMouseDown / IsMouseTriggered / IsMouseReleased(MouseButton::Left/Right/Middle)` | マウスのボタン |
| `MousePosition()` / `MouseDelta()` | 位置[px]（窓の左上が0,0） / このフレームの移動量 |
| `IsPadConnected()` | パッドが繋がっているか |
| `IsPadDown / IsPadTriggered / IsPadReleased(XINPUT_GAMEPAD_A 等)` | パッドのボタン |
| `PadLeftX()` / `PadLeftY()` / `PadRightX()` / `PadRightY()` | スティック（-1〜1、上が正） |
| `PadLeftTrigger()` / `PadRightTrigger()` | トリガー（0〜1） |
| `IsWindowActive()` | 窓が手前にあるか（裏にあるときは全部「押されていない」） |

---

## 7. 音（SoundBank）

WAVだけ読める。名前を付けて登録し、名前で鳴らす。

```cpp
engine.Sounds().Register("jump", "resources/Audio/jump.wav");
engine.Sounds().Register("bgm",  "resources/Audio/bgm.wav", 0.8f);  // 音ごとの音量
engine.Sounds().LoadAll(engine.Audio());   // Registerし終えてから1回

engine.Sounds().Play("jump");
engine.Sounds().PlayMusic("bgm");          // ループ再生
```

| 関数・項目 | 説明 |
|---|---|
| `Register(名前, パス, 音量=1)` | 登録（まだ読まない） |
| `LoadAll(engine.Audio())` | 登録したものを読む |
| `Play(名前, 音量=1, ピッチ=1)` | 効果音 |
| `PlayMusic(名前)` / `StopMusic()` | 曲（1曲だけ） |
| `SetMuted(bool)` | 消音 |
| `masterVolume` / `musicVolume` | 全体 / 曲の音量（変えたら `ApplyVolumes()`） |

---

## 8. 物理と衝突

物理は 1/60 秒刻みで動く。`usePhysics = true` の物が重力で落ち、`isStatic = true` の物に当たって止まる。

`engine.Physics()` の設定：`gravity`(-9.8) / `linearDamping` / `angularDamping` / `solverIterations`

### CollisionEvent

| 項目 | 説明 |
|---|---|
| `a`, `b` | 触れた2つ（どちらがaかは決まっていない） |
| `phase` | `CollisionPhase::Enter`(触れた瞬間) / `Stay` / `Exit`(離れた) |
| `isTrigger` | トリガーとの重なり（押し返しは無い） |
| `point` / `normal` | 接触点 / bからaへの向き |
| `impulse` | ぶつかった強さ |
| `Involves("Tag")` | どちらかがそのタグか |
| `WithTag("Tag")` | そのタグの方を返す（無ければnullptr） |
| `Other(self)` | selfではない方 |

```cpp
engine.SetCollisionCallback([&](const CollisionEvent& e) {
    if (e.phase != CollisionPhase::Enter) { return; }
    if (Object3D* coin = e.WithTag("Coin"); coin && e.Involves("Player")) {
        engine.Destroy(coin->id);
    }
});
```

1フレームに2回呼ばれることがある。「1回だけ」の処理は `Enter` だけを見る。

---

## 9. 画面に出す（文字・2D）

### 文字

座標は **1280x720 の板**の上の値。実際の窓の大きさに合わせて自動で拡縮される。
`SetUpdateCallback` の中で**毎フレーム**呼ぶ。

| 関数 | 説明 |
|---|---|
| `DrawText(x, y, "文字", 大きさ=24, 色, 影=true, 書体)` | 左上を指定して出す |
| `DrawTextCentered(y, "文字", 大きさ, 色, 書体)` | 横の真ん中に出す |
| `MeasureTextWidth("文字", 大きさ, 書体)` | 幅を測る |
| `WorldToUi(3Dの位置, cam.GetViewProjectionMatrix(), x, y)` | 3Dの位置を板の座標へ（頭上に名前を出すなど） |

書体 `TextFace::Display`(見出し) / `Body`(本文・日本語) / `Numeric`(数字) / `Label`(注記)。
板の大きさは `ScreenWidth()` / `ScreenHeight()`（既定 1280x720。2Dの描画と共通で、`Get2DCanvas()` から変えられる）。端の余白は定数 `Engine::kUiMarginX` / `kUiMarginY`。

### 2Dの図形と絵

座標は **1280x720 の画面のピクセル**（左上が原点・下がプラス）。毎フレーム呼ぶ。呼んだ順に重なる。

| 関数 | 説明 |
|---|---|
| `Rect2D(左, 上, 右, 下, 色)` | 四角 |
| `RectCentered2D(中心x, 中心y, 幅, 高さ, 回転, 色)` | 傾けられる四角 |
| `Circle2D(x, y, 半径, 色)` | 円 |
| `Line2D(x0, y0, x1, y1, 太さ, 色)` | 線 |
| `Sprite2D("resources/…png", x, y, 回転=0, 色=白, 倍率=1)` | 絵（元の大きさ） |
| `Sprite2DSized(path, x, y, 幅, 高さ, 回転, 色)` | 絵（大きさ指定） |
| `Sprite2DRegion(path, x, y, 元のx, 元のy, 元の幅, 元の高さ, 回転, 色, 倍率)` | 絵の一部を切り取って出す（コマ送り・アイコンの並んだ絵） |
| `Sprite2DSize(path)` | 絵の大きさ |
| `HudRect2D` / `HudCircle2D` / `HudSprite2D` / `HudSprite2DRegion` | 2Dカメラを通さない版（UI用） |
| `Get2DCamera()` | 2Dカメラ：`focus`(見る場所) / `zoom` / `Shake(強さ)` |

3Dの上・文字の下に描かれる。

---

## 10. 部品（Component）

オブジェクトに後から機能を挿す仕組み。

### 最初から入っている部品

| 部品 | 主な項目 | 動き |
|---|---|---|
| `SpinComponent` | `axis`, `degreesPerSecond` | 回り続ける |
| `BobComponent` | `amplitude`, `cyclesPerSecond`, `phase`, `direction` | ふわふわ揺れる |
| `PatrolComponent` | `offset`, `secondsOneWay`, `holdSeconds`, `smooth` | 往復する |
| `SoundOnHitComponent` | `cue`, `onlyTag`, `minImpulse`, `volume` | 触れたら音を鳴らす |
| `VanishOnTouchComponent` | `onlyTag`, `respawnSeconds` | 触れたら消える（見えなく・当たらなくなる） |
| `HighlightOnTouchComponent` | `touchedColor`, `onlyTag` | 触れている間だけ色が変わる |

```cpp
obj.AddComponent<SpinComponent>()->degreesPerSecond = 180.0f;
```

### 自分で作る

```cpp
// Game/Blink.h
#pragma once
#include "../Engine/Engine.h"

class BlinkComponent : public ComponentOf<BlinkComponent> {
public:
    float interval = 0.5f;

    void OnStart(ComponentContext& ctx) override { (void)ctx; timer_ = 0.0f; }
    void OnUpdate(ComponentContext& ctx) override {
        timer_ += ctx.deltaTime;
        if (timer_ >= interval) { timer_ = 0.0f; ctx.self.visible = !ctx.self.visible; }
    }
private:
    float timer_ = 0.0f;
};
```

| 関数（上書きする） | いつ呼ばれるか |
|---|---|
| `OnStart(ctx)` | 最初の `OnUpdate` の直前に1回 |
| `OnUpdate(ctx)` | 毎フレーム（`SetTimeScale`がかかる） |
| `OnCollision(ctx, hit)` | 触れたとき。`hit.other` / `hit.phase` / `hit.normal`（自分を押し出す向き） / `hit.impulse` |
| `AllowsMultiple()` | `true`を返すと1つの物に複数付けられる |

`ctx` の中身：`self`(付いている物) / `deltaTime` / `timeInScene` / `services->PlayCue(名前, 音量)` / `services->FindByTag(タグ)`

`enabled = false` で止まる。

---

## 11. 数学

`Vector2` `Vector3` `Vector4` は `{x, y, z, w}` の素の構造体（演算子は無い）。

| 関数 | 説明 |
|---|---|
| `MakeAffineMatrix(scale, rotate, translate)` | 拡縮・回転・移動の行列 |
| `Multiply(a, b)` | 行列の掛け算 |
| `Inverse4x4(m)` | 逆行列 |
| `TransformPoint(p, m)` | 点を行列で変換 |
| `MakePerspectiveMatrix` / `MakeOrthographicMatrix` | 投影行列 |
| `QuaternionFromAxisAngle(axis, rad)` / `QuaternionFromEulerXYZ(euler)` | クォータニオンを作る |
| `RotateVector(q, v)` / `Multiply(q1, q2)` / `Normalize(q)` | クォータニオンの計算 |
| `Log("文字")` | `logs/latest.log` と VSの出力ウィンドウへ |

---

## 12. 気をつけること

- **`Object3D&` を持ち続けない。** 物は `std::vector` に入っているので、1体増えただけで全部が引っ越すことがある。持っておくのは `id`、使うときに `FindById`。
- **衝突の通知の中で Add系を呼ばない。** 消すのは `Destroy` なら安全（フレームの終わりに消える）。
- **文字・2Dは毎フレーム呼ぶ。** 1回呼んだら出続ける作りではない。
- **ファイルのパスは実行時のフォルダから見た場所。** VSから起動するとプロジェクトのフォルダが実行時のフォルダになる。
- **警告はエラー扱い。** 使っていない引数は `(void)dt;` のように消しておく。
- 床は薄くしすぎない。速く落ちる物が1ステップで通り抜けることがある（連続判定は無い）。

---

## 13. デバッグビュー（F2）

起動中に **F2** を押すと、ゲームを一時停止して Unity の Scene ビューのような画面に切り替わる。
画面の縁がオレンジになっている間がデバッグビュー。もう一度 **F2** でゲームへ戻る（動かした結果はそのまま残る）。

| 操作 | 動き |
|---|---|
| 左クリック | 物を選ぶ（オレンジの枠が付く）。何も無い所をクリックで選択を外す |
| **W** / **E** / **R** | 移動（矢印）/ 回転（輪）/ 拡縮（先端に四角）のギズモに切り替える |
| ギズモの軸をドラッグ | その軸に沿って動かす・回す・伸ばす。軸の色は X 赤・Y 緑・Z 青、掴んでいる軸は黄色 |
| Ctrl を押しながらドラッグ | 刻んで動かす（移動 0.25 / 回転 15度 / 拡縮 0.1） |
| 右ドラッグ | 視点を回す。押している間は **WASD / QE** で飛べる（Shiftで速く） |
| **F** | 選んだ物へ寄る |

- デバッグビューの視点は、ゲームのカメラとは別に持っている。戻るとゲームの視点に戻る
- 止まっている間は、物理・部品・`SetUpdateCallback` が呼ばれない（ゲームのHUDも消える）
- ギズモは物の `transform.translate`（原点）に出る。回転はワールドの X/Y/Z 軸まわり
- **Debug ビルドでだけ**使える。Release（提出用）では F2 を押しても出ない

| 関数 | 説明 |
|---|---|
| `SetDebugViewEnabled(bool)` | デバッグビューを使えるか（既定：Debugでtrue・Releaseでfalse） |
| `IsDebugViewActive()` | 今デバッグビューか |
| `DebugSelectedId()` | デバッグビューで選んでいる物のid（0なら無し） |
