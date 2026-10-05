// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#pragma once
#include "Graphics/Renderer.h"
#include "Graphics/TextRenderer.h"
#include "Graphics/Particle.h"         // パーティクル（Instancingで粒をまとめて描く）
#include "Graphics/RenderFeatures.h"   // どのシェーダーを通すか（影など）
#include "Graphics/Draw2D.h"           // 2Dの座標とカメラ
#include "Physics/PhysicsWorld.h"
#include "Core/SoundBank.h"            // 名前で引ける音の置き場（どの音を入れるかはゲームが決める）
#include "Components/BuiltinComponents.h" // オブジェクトに挿せる部品（回る・往復する・触れたら鳴らす…）
#include "Editor/ObjectInspector.h"          // デバッグビュー中に出す、オブジェクトの設定パネル
#include "Editor/ObjectEdits.h"              // デバッグビューで変えた内容の保存と読み込み
#include "DebugView/GizmoMath.h"             // デバッグビュー（F2）のギズモの計算

// レイがオブジェクトに当たった結果（Engine::Raycast）
struct RaycastHit {
    Object3D* object = nullptr; // 当たったオブジェクト（当たらなければnullptr）
    float distance = 0.0f;      // レイの始点からの距離
    Vector3 point{};            // 当たった場所
};

// ============================================================================
// Engine：DirectX12・入力・音・物理をまとめて隠すクラス
//
// ゲームのコードから触るのはこのクラスだけでよい。エディタは無い。
// 何を置くか・毎フレーム何をするかは、すべてコード（Game/ 以下）で書く。
//
// 1フレームの流れ（Run）：
//   入力 → 物理（固定1/60秒、衝突を通知） → 部品の更新 → ゲームの更新(SetUpdateCallback)
//   → 消す予約の実行 → カメラ確定 → 描画
// ============================================================================
class Engine : public ComponentServices {
public:
    // ウィンドウ生成からDirectX12初期化まで一式行う。main.cppが呼ぶ。
    // fullscreen=true の場合、clientWidth/clientHeightは使われず、
    // デスクトップの解像度でボーダーレスフルスクリーン起動する
    void Initialize(const wchar_t* title, int32_t clientWidth, int32_t clientHeight, bool fullscreen = true,
                    SurfaceColor surfaceColor = SurfaceColor::Srgb) {
        CoInitializeEx(0, COINIT_MULTITHREADED);
        InitializeLogFile();

        fullscreen_ = fullscreen;
        windowedClientWidth_ = clientWidth;   // 窓へ戻すときの大きさ（全画面で始めた場合も、引数の大きさへ戻す）
        windowedClientHeight_ = clientHeight;
        hwnd_ = CreateGameWindow(title, clientWidth, clientHeight, fullscreen); // fullscreen時はclientWidth/Heightが実解像度に書き換わる
#ifdef _DEBUG
        EnableDebugLayer();
#endif
        gfx_ = CreateGraphicsContext(hwnd_, clientWidth, clientHeight, surfaceColor);
        pipeline_ = CreateObject3DPipeline(gfx_.device.Get());
        linePipeline_ = CreateLinePipeline(gfx_.device.Get(), pipeline_);
        shadowPipeline_ = CreateShadowPipeline(gfx_.device.Get(), pipeline_);
        lineBoxMesh_ = CreateLineBoxMesh(gfx_.device.Get());
        shadowQuadMesh_ = CreateShadowQuadMesh(gfx_.device.Get());
        draw2DPipeline_ = CreateDraw2DPipeline(gfx_.device.Get(), pipeline_); // 2Dの図形と絵
        draw2DQuadMesh_ = CreateDraw2DQuadMesh(gfx_.device.Get());
        draw2DCircleMesh_ = CreateDraw2DCircleMesh(gfx_.device.Get());
        for (int i = 0; i < kMaxDraw2DRects; ++i) { draw2DTransformBuffers_.push_back(CreateLineTransformBuffer(gfx_.device.Get())); }
        for (int i = 0; i < kMaxShadows; ++i) { shadowTransformBuffers_.push_back(CreateLineTransformBuffer(gfx_.device.Get())); }
        // 当たり判定の枠（デバッグ表示）用のWVP定数バッファ。1本を使い回すと前の値を上書きしてしまうので数を用意する
        for (int i = 0; i < kLineTransformBufferCount; ++i) { lineTransformBuffers_.push_back(CreateLineTransformBuffer(gfx_.device.Get())); }
        textures_ = LoadDefaultTextures(gfx_);
        light_ = CreateDirectionalLightResource(gfx_.device.Get());
        text_.Initialize(gfx_, pipeline_);
        particles_.Initialize(gfx_, pipeline_);
#ifdef USE_IMGUI
        InitializeImGui(hwnd_, gfx_, surfaceColor == SurfaceColor::Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                        : DXGI_FORMAT_R8G8B8A8_UNORM);
        imguiInitialized_ = true;
#endif
        input_.Initialize(hwnd_);
        camera_.Initialize((float)clientWidth / (float)clientHeight, &input_);
        debugCamera_.Initialize((float)clientWidth / (float)clientHeight, &input_);
        debugCamera_.SetFreeMove(true, true); // 右ボタンを押している間だけ飛べる（Unityと同じ）
        draw2DTriangleMesh_ = CreateDraw2DCircleMesh(gfx_.device.Get(), 3); // ギズモの矢じり
        audio_.Initialize();
        // 音そのものはここでは読まない。ゲームがRegisterしてからLoadAll()する（SoundBank.hを参照）

        initialized_ = true;
    }

    // ===== オブジェクトを置く =====
    //
    // ここから置いた物にはuvChecker.pngが貼られる。
    // 絵を貼りたくないときは Object3D::untextured を true にすると、色だけで描く

    // 球を置きたいだけならこれを1行呼ぶだけでよい
    Object3D& AddSphere() { return AddPrimitive(PrimitiveKind::Sphere); }
    // スプライト（平行投影の板ポリゴン）を置きたいだけならこれを1行呼ぶだけでよい
    Object3D& AddSprite() { return AddPrimitive(PrimitiveKind::Sprite); }
    // 立方体を置きたいだけならこれを1行呼ぶだけでよい
    Object3D& AddCube() { return AddPrimitive(PrimitiveKind::Cube); }

    // 3つとも同じ手順なので1か所にまとめてある
    Object3D& AddPrimitive(PrimitiveKind kind) {
        objects_.push_back(CreateObject3D(gfx_.device.Get(), kind));
        objects_.back().untextured = false;
        return objects_.back();
    }

    // objファイルを1体配置したいだけならこれを1行呼ぶだけでよい
    // 同じdirectoryPath+filenameを2回目以降に呼んだ場合は、ファイル読み込みやGPUアップロードをやり直さず
    // キャッシュ済みのメッシュ/テクスチャを共有する（Sphere/Cubeを何体も置くのと同じ感覚で使える）
    // objの中が o / g で複数に分かれている場合は、その数だけ別々のオブジェクトとして置く。
    // まとめて1つにしてしまうと、車体とタイヤのように分かれているモデルを個別に動かせない
    // （戻り値は最初の部品。全部のIDが要るなら AddModelParts を使う）
    Object3D& AddModel(const std::string& directoryPath, const std::string& filename) {
        std::vector<uint32_t> ids = AddModelParts(directoryPath, filename);
        // AddModelPartsは読めなかった場合もデバッグ用の立方体を1つ置くので、通常ここは空にならない。
        // それでも参照を返す以上、何も無いまま先頭を取りに行くわけにはいかないので受け止めておく
        if (ids.empty()) {
            Log("[Model] \"" + directoryPath + "/" + filename + "\" から何も置けませんでした。立方体で代用します");
            return AddCube();
        }
        return *FindObjectById(ids.front());
    }

    // objの部品をすべて置き、置いたオブジェクトのIDを順に返す。
    // 添字ではなくIDを返すのは、この後に別のものを足したり消したりしても指し先がずれないようにするため
    std::vector<uint32_t> AddModelParts(const std::string& directoryPath, const std::string& filename) {
        std::vector<std::string> keys = GetOrLoadCachedModelKeys(directoryPath, filename);
        std::vector<uint32_t> ids;
        ids.reserve(keys.size());
        for (const std::string& key : keys) {
            uint32_t id = AddCachedModelByKey(key);
            if (id != 0) { ids.push_back(id); }
        }
        return ids;
    }

    // 読み込み済みモデルのキャッシュ一覧
    const std::unordered_map<std::string, CachedModelData>& ModelCache() const { return modelCache_; }

    // 配置済みオブジェクト一覧（ゲーム側で毎フレーム調整したい場合に使う）
    std::vector<Object3D>& Objects() { return objects_; }

    // 音声再生（WAVの読み込み・再生・停止・音量調整）はこちら経由で行う
    // 例：SoundData se = engine.Audio().LoadWave("resources/Audio/se_hit.wav");
    //     engine.Audio().Play(se);
    AudioEngine& Audio() { return audio_; }

    // キーボード入力（DirectInput）。ゲームロジックから直接キーを見たいときに使う
    // 例：if (engine.Input().IsKeyDown(DIK_R)) { /* リスタート */ }
    InputEngine& Input() { return input_; }

    // ===== 窓の実際の大きさと handle =====
    //
    // 下の ScreenWidth/ScreenHeight とは別物。あちらは画面表示を組むための
    // 板の大きさ（1280x720で固定）で、こちらは実際に出ている窓のピクセル数
    int32_t WindowPixelWidth() const { return gfx_.clientWidth; }
    int32_t WindowPixelHeight() const { return gfx_.clientHeight; }
    HWND WindowHandle() const { return hwnd_; }

    // D3D12のデバイス（自分用のリソースを作りたいとき向け）。参照カウントは触らない生ポインタ
    ID3D12Device* GraphicsDevice() const { return gfx_.device.Get(); }

    // ===== 画面に文字を出す =====
    // 座標は1280x720の板の上の値（下の説明を参照）。sizeは文字の高さ
    //   engine.DrawText(20.0f, 20.0f, "スコア 120", 32.0f);
    //
    // 読みやすさのため、既定で少し下にずらした暗い文字を先に重ねる（背景が明るくても沈まないようにする）。
    // 3D世界の上に出すUIなので、影が無いと空や明るい床の上でほとんど読めなくなる
    // faceで書体を選べる（TextFace::Display / Body / Numeric / Label）。

    // ===== 画面表示の座標系（1280x720の板） =====
    // 画面表示（HUD）は、1280x720の板の上に組んである。
    // 実際の画面がどんな大きさでも、この板を縦横同じ倍率で拡大し、
    // 余った側には均等な余白を置いて中央へ収める（レターボックス）。
    //
    // 実際の画面の大きさをそのまま使わないのは、次の2つが同時に起きるため。
    //   ・高い解像度ほど文字が豆粒になる（大きさをピクセルで書いているから）
    //   ・文字だけ大きくすると、位置は動かないので行が重なり、端からはみ出す
    // 板ごと拡大すれば、大きさも間隔も位置も全部そろって動く。
    // 縦横同じ倍率にするのは、比率の違う画面で絵が引き伸ばされないようにするため。
    //
    // 端いっぱいまでは使わない。放送でいうセーフエリアと同じ考えで、
    // 置くときは kUiMarginX / kUiMarginY だけ内側から始めるとよい。
    static constexpr float kUiWidth = 1280.0f;
    static constexpr float kUiHeight = 720.0f;
    // 端からの余白。放送の「タイトルセーフ」（端から5%）に近い取り方で、
    // 端に寄りすぎず、かつ詰まって見えない幅
    static constexpr float kUiMarginX = 44.0f; // 横は板の3.4%
    static constexpr float kUiMarginY = 32.0f; // 縦は板の4.4%

    // 行の高さ。文字の大きさに対する倍率で決める。
    // ここをピクセルで決め打ちにすると、文字を大きくしたときに行が重なる
    static constexpr float kUiLineHeight = 1.35f;
    static float UiLine(float size) { return size * kUiLineHeight; }

    // 全画面（ボーダーレス）の出し入れ。F11 か Alt+Enter でも切り替わる。
    // 窓へ戻すときは、全画面にする前の位置と大きさへ戻る
    bool IsFullscreen() const { return fullscreen_; }
    void SetFullscreen(bool fullscreen) {
        if (fullscreen == fullscreen_) { return; }

        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(MONITORINFO) };
        GetMonitorInfo(monitor, &info);
        const RECT& screen = info.rcMonitor;

        if (fullscreen) {
            GetWindowRect(hwnd_, &windowedRect_);
            hasWindowedRect_ = true;
            SetWindowLongPtr(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd_, HWND_TOP, screen.left, screen.top,
                screen.right - screen.left, screen.bottom - screen.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            RECT rect = windowedRect_;
            if (!hasWindowedRect_) {
                // 全画面で始めたので戻り先が無い。画面の真ん中へ、引数で渡された大きさで置く
                rect = { 0, 0, windowedClientWidth_, windowedClientHeight_ };
                AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
                int w = rect.right - rect.left, h = rect.bottom - rect.top;
                int x = screen.left + ((screen.right - screen.left) - w) / 2;
                int y = screen.top + ((screen.bottom - screen.top) - h) / 2;
                rect = { x, y, x + w, y + h };
            }
            SetWindowLongPtr(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPos(hwnd_, HWND_NOTOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        fullscreen_ = fullscreen;

        // 実際の描画領域の大きさに合わせて、大きさに依存するものを作り直す
        RECT client{};
        GetClientRect(hwnd_, &client);
        ResizeGraphicsContext(gfx_, client.right - client.left, client.bottom - client.top);
        camera_.SetAspectRatio(ViewAspect());
        debugCamera_.SetAspectRatio(ViewAspect());
    }
    void ToggleFullscreen() { SetFullscreen(!fullscreen_); }

    // 画面の縦横比。視野に何メートル入るかの計算に使う
    float ViewAspect() const {
        return (gfx_.clientHeight > 0) ? ((float)gfx_.clientWidth / (float)gfx_.clientHeight) : (16.0f / 9.0f);
    }

    // 板1マスが実際の画面で何ピクセルになるか
    float UiScale() const {
        return (std::min)((float)gfx_.clientWidth / kUiWidth, (float)gfx_.clientHeight / kUiHeight);
    }
    // 板の左上が実際の画面のどこに来るか（余った側の余白ぶん）
    float UiOriginX() const { return ((float)gfx_.clientWidth - kUiWidth * UiScale()) * 0.5f; }
    float UiOriginY() const { return ((float)gfx_.clientHeight - kUiHeight * UiScale()) * 0.5f; }

    // 板の上の座標を、実際の画面の座標へ移す（と、その逆）
    float UiToScreenX(float x) const { return UiOriginX() + x * UiScale(); }
    float UiToScreenY(float y) const { return UiOriginY() + y * UiScale(); }
    float ScreenToUiX(float x) const { return (x - UiOriginX()) / UiScale(); }
    float ScreenToUiY(float y) const { return (y - UiOriginY()) / UiScale(); }

    // 3Dの点を板の上の座標へ。世界に貼りつく表示（頭の上の名前・その場に出す数字）に使う
    bool WorldToUi(const Vector3& world, const Matrix4x4& viewProjection, float& outX, float& outY) {
        float screenX = 0.0f, screenY = 0.0f;
        if (!WorldToScreen(world, viewProjection, (float)gfx_.clientWidth, (float)gfx_.clientHeight,
                           screenX, screenY)) {
            return false;
        }
        outX = ScreenToUiX(screenX);
        outY = ScreenToUiY(screenY);
        return true;
    }

    // 数字や欧文だけの短いものはNumeric/Labelが映えるが、日本語には字が無いので
    // 日本語を含むものはDisplayかBodyにすること（無い字はBodyで代わりに出る）。
    //
    // x/y/size はすべて板の上（1280x720）の値。実際の画面へはここで移す
    void DrawText(float x, float y, const std::string& utf8Text, float size = 24.0f,
                  const Vector4& color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }, bool shadow = true,
                  TextFace face = TextFace::Body) {
        float screenX = UiToScreenX(x);
        float screenY = UiToScreenY(y);
        float screenSize = size * UiScale();
        if (shadow) {
            float offset = (std::max)(1.0f, screenSize * 0.06f);
            text_.DrawText(gfx_.commandList.Get(), screenX + offset, screenY + offset, utf8Text, screenSize,
                           Vector4{ 0.0f, 0.0f, 0.0f, color.w * 0.6f }, face);
        }
        text_.DrawText(gfx_.commandList.Get(), screenX, screenY, utf8Text, screenSize, color, face);
    }

    // 板の横中央にそろえて出す。題や見出しのように「真ん中に置きたい」ものに使う
    void DrawTextCentered(float y, const std::string& utf8Text, float size = 24.0f,
                          const Vector4& color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
                          TextFace face = TextFace::Body) {
        float x = (kUiWidth - MeasureTextWidth(utf8Text, size, face)) * 0.5f;
        DrawText(x, y, utf8Text, size, color, true, face);
    }

    // 文字列を描いたときの幅。板の上の値で返るので、DrawTextに渡すのと同じ物差しで測れる。
    // 測る前に文字を用意するので、その文字列が初めて出るフレームでも正しい幅が返る
    float MeasureTextWidth(const std::string& utf8Text, float size = 24.0f, TextFace face = TextFace::Body) {
        text_.EnsureGlyphs(gfx_.commandList.Get(), utf8Text, face);
        return text_.MeasureWidth(utf8Text, size, face);
    }

    // 画面表示を組むときの板の大きさ。実際の画面のピクセル数ではない
    // （実際の大きさが要る場面はここには無い。あるとすれば、それは板の上で組めていない）
    float ScreenWidth() const { return kUiWidth; }
    float ScreenHeight() const { return kUiHeight; }

    // ===== 毎フレームの処理 =====
    //   engine.SetUpdateCallback([&](float deltaTime) { /* 毎フレームやりたい処理 */ });
    // deltaTimeは実際の経過時間[秒]（SetTimeScaleの影響を受けない）。
    // 物理・部品の後、描画の前に呼ばれる
    void SetUpdateCallback(std::function<void(float)> callback) { updateCallback_ = std::move(callback); }

    // ImGuiのフレームの中で毎フレーム呼ばれる処理を登録する。ここでImGui::Begin/ColorEdit4などを呼ぶ。
    // SetUpdateCallbackはImGuiのフレームが始まる前に走るので、そこからImGui::は呼べない。
    // ImGuiの入らない構成（Release）では呼ばれない
    void SetImGuiCallback(std::function<void(float)> callback) { imguiCallback_ = std::move(callback); }

    // 衝突・トリガーが起きたときに呼ばれる処理を登録する：
    //   engine.SetCollisionCallback([&](const CollisionEvent& e) {
    //       if (e.phase == CollisionPhase::Enter) { /* ぶつかった瞬間の処理 */ }
    //   });
    //
    // 物理は固定の時間刻みで動くので、1フレームに2回以上呼ばれることがある。
    //
    // eventの中身：
    //   e.a / e.b       触れた2つ。どちらがa/bになるかは決まっていないので両方調べること
    //   e.phase         Enter(触れた瞬間) / Stay(触れ続けている) / Exit(離れた)
    //   e.isTrigger     trueなら押し返さないトリガーとの重なり（ゴール地点など）
    //   e.impulse       押し返しに使った力の合計。着地音の音量などにそのまま使える
    //   e.point/normal  接触した位置と向き
    //
    // この中でAdd系を呼ばないこと（e.a / e.b が古い場所を指すことになる）。
    // 消したいときは Destroy() を呼べば、フレームの終わりにまとめて消える
    void SetCollisionCallback(std::function<void(const CollisionEvent&)> callback) { collisionCallback_ = std::move(callback); }

    // ===== オブジェクトを探す・消す =====
    //
    // Add系が返すObject3D&を後で使い回さないこと。objects_はstd::vectorなので、
    // 1体増やすたびに全部が別の場所へ引っ越すことがあり、前に貰った参照は古い場所を指す。
    // idを控えておいて、使うときにここで引き直す
    Object3D* FindById(uint32_t id) { return FindObjectById(id); }

    // ----- パーティクル -----
    // 粒を出す口を1つ足す。返り値の番号で、あとから設定を変えたり、まとめて出したりする。
    // 設定はデバッグビュー（F2）のParticlesパネルからも変えられ、変えた内容は保存される
    int AddParticleEmitter(const ParticleEmitter& emitter) { return particles_.AddEmitter(emitter); }
    ParticleEmitter& GetParticleEmitter(int id) { return particles_.Emitter(id); }
    // 一度にまとめて出す（爆発・着弾など）
    void EmitParticles(int id, int count) { particles_.Burst(id, count); }
    // 位置を指定して1つ出す（板をInstancingで並べたいときなど）
    void EmitParticleAt(int id, const Vector3& position) { particles_.SpawnAt(id, position); }

    // 名前・タグで最初の1体を探す（見つからなければnullptr）
    Object3D* FindByName(const std::string& name) {
        for (Object3D& obj : objects_) { if (obj.name == name) { return &obj; } }
        return nullptr;
    }
    Object3D* FindByTag(const std::string& tag) override {
        for (Object3D& obj : objects_) { if (obj.tag == tag) { return &obj; } }
        return nullptr;
    }
    // タグが一致するものを全部（idの一覧で返す）
    std::vector<uint32_t> FindAllByTag(const std::string& tag) const {
        std::vector<uint32_t> ids;
        for (const Object3D& obj : objects_) { if (obj.tag == tag) { ids.push_back(obj.id); } }
        return ids;
    }

    // 消す。その場では消さず、このフレームの終わり（描画の前）にまとめて消す。
    // 衝突の通知や部品の更新の最中に呼んでも安全
    void Destroy(uint32_t id) { if (id != 0) { pendingDestroy_.push_back(id); } }
    void DestroyAll() { destroyAllRequested_ = true; }

    // ===== 時間 =====
    // 物理と部品にかける時間の倍率。0で止まる（ポーズ）、0.5でスロー。
    // ゲームの更新（SetUpdateCallback）には実際の時間がそのまま渡るので、
    // ポーズ中もメニューの操作などは書ける
    void SetTimeScale(float scale) { timeScale_ = (scale < 0.0f) ? 0.0f : scale; }
    float TimeScale() const { return timeScale_; }
    // 起動してからの通算[秒]（実時間）
    float TotalTime() const { return totalTime_; }
    // 直前のフレームの経過時間[秒]（実時間）
    float DeltaTime() const { return lastFrameTime_; }

    // 物理ワールドの設定（重力・空気抵抗・接触の解き直し回数）
    PhysicsWorld& Physics() { return physics_; }

    // ===== 見た目 =====
    // 影など、どのシェーダーを通すか。詳しくは Engine/Graphics/RenderFeatures.h
    RenderFeatures& Features() { return features_; }
    const RenderFeatures& Features() const { return features_; }
    void SetShadows(bool on) { features_.shadows = on; }
    bool ShadowsEnabled() const { return features_.shadows; }

    // 何も描いていない所の色（背景）
    void SetClearColor(const Vector4& color) { clearColor_ = color; }

    // 平行光源（向き・色・強さ）
    void SetLightDirection(const Vector3& direction) { light_.data->direction = direction; }
    void SetLightColor(const Vector4& color) { light_.data->color = color; }
    void SetLightIntensity(float intensity) { light_.data->intensity = intensity; }

    // 当たり判定の枠を線で出す（デバッグ用。緑＝触れていない、赤＝触れている）
    void SetShowColliders(bool show) { showColliders_ = show; }

    // ===== 音 =====
    //     engine.Sounds().Register("hit", "resources/Audio/hit.wav");
    //     engine.Sounds().LoadAll(engine.Audio());   // Registerし終えてから1回
    //     engine.Sounds().Play("hit");
    SoundBank& Sounds() { return sounds_; }

    // ===== カメラ =====
    //   engine.GetCamera().SetPosition({ 0, 5, -10 });
    //   engine.GetCamera().LookAt({ 0, 0, 0 });
    Camera& GetCamera() { return camera_; }

    // ===== マウスで指したものを調べる =====
    // 画面のピクセル座標（左上が0,0）から、カメラを通って奥へ伸びるレイを作る。
    // 窓の外ならfalse
    bool ScreenPointToRay(int pixelX, int pixelY, Vector3& outOrigin, Vector3& outDirection) {
        POINT pt{ pixelX, pixelY };
        return ComputeMouseRay(pt, outOrigin, outDirection);
    }
    // 今のマウス位置から伸びるレイ
    bool MouseRay(Vector3& outOrigin, Vector3& outDirection) {
        return ComputeMouseRay(input_.MousePosition(), outOrigin, outDirection);
    }

    // レイを飛ばして、一番手前で当たったオブジェクトを返す（当たり判定の箱で調べる）。
    // 見えていないもの・スプライトは対象外。tagが空でなければ、そのタグのものだけを見る
    RaycastHit Raycast(const Vector3& origin, const Vector3& direction,
                       float maxDistance = 1000.0f, const std::string& tag = "") {
        RaycastHit result;
        float best = maxDistance;
        for (Object3D& obj : objects_) {
            if (!obj.visible || obj.isSprite) { continue; }
            if (!tag.empty() && obj.tag != tag) { continue; }
            float distance = 0.0f;
            if (RayIntersectOBB(origin, direction, obj.GetWorldOBB(), distance) && distance < best) {
                best = distance;
                result.object = &obj;
                result.distance = distance;
                result.point = { origin.x + direction.x * distance, origin.y + direction.y * distance,
                                 origin.z + direction.z * distance };
            }
        }
        return result;
    }

    // ===== その他 =====
    // 画面を撮る（screenshots/ にpngで残る）。F12でも撮れる
    void RequestScreenshot() { screenshotRequested_ = true; }
    void SetScreenshotKeyEnabled(bool enabled) { screenshotKeyEnabled_ = enabled; }

    // ゲームを終わる（窓を閉じたのと同じ）
    void Quit() { PostQuitMessage(0); }

    // ===== デバッグビュー（F2）=====
    // 起動中にF2を押すと、ゲームを止めて Unity の Scene ビューのような画面に切り替わる。
    //   左クリック        物を選ぶ（何も無い所で選択を外す）
    //   W / E / R         移動 / 回転 / 拡縮 のギズモ
    //   ギズモをドラッグ  選んだ物を動かす（Ctrlを押しながらだと刻んで動く）
    //   右ドラッグ        視点を回す。押している間は WASD / QE で飛べる（Shiftで速く）
    //   F                 選んだ物へ寄る
    //   F2                ゲームへ戻る（動かした結果はそのまま残る）
    // 視点はゲームのカメラとは別に持っているので、戻るとゲームの視点に戻る。
    // 既定では Debug ビルドでだけ使える（Release では F2 を押しても出ない）
    void SetDebugViewEnabled(bool enabled) {
        debugViewEnabled_ = enabled;
        if (!enabled && debugViewActive_) { SetDebugViewActive(false); }
    }
    bool IsDebugViewActive() const { return debugViewActive_; }
    // デバッグビューで選んでいる物のid（選んでいなければ0）
    uint32_t DebugSelectedId() const { return debugSelectedId_; }

    // ===== 2Dのゲームとして映す =====
    //
    // カメラを平行投影に切り替え、縦に何メートル映すかを決める。
    // 2Dのゲームは「奥行きで大きさが変わらない」ことが前提になっていて、
    // 透視投影のままだと同じ大きさの物が画面の端と真ん中で違う大きさに映る。
    //
    // heightInUnits は縦に映す長さ。1マス=1mでタイルを並べるなら、
    // 10を渡せば画面に縦10マス入る。
    //
    // 物の側は RotationMode::PlanarZ にしておくこと。
    // 平行投影で見ているだけでは奥（Z）へ倒れるのは止まらないので、
    // 横から見ているのに手前へ倒れる、ということが起きる
    void SetCamera2D(float heightInUnits) {
        camera_.SetProjection(CameraProjection::Orthographic);
        camera_.SetOrthoHeight(heightInUnits);
        Log(std::format("[Camera] 平行投影（2D）にしました。縦に{:.1f}メートル映します", heightInUnits));
    }

    void SetCamera3D() {
        camera_.SetProjection(CameraProjection::Perspective);
        Log("[Camera] 透視投影（3D）に戻しました");
    }

    // ========================================================================
    // 2Dの描画
    //
    // 画面のピクセル（左上が原点・下がプラス）で描く。詳しくは Engine/Render/Draw2D.h。
    //
    // 呼び方は「毎フレーム描く」形にしてある。シーンに物として置く形ではない。
    // 2Dのゲームは「今フレームの絵を上から順に積む」書き方が素直で、
    // ImGuiの描画リストやDXライブラリもその形をしている。
    // 置いた物として持たせると、消し忘れや重なり順の管理が全部こちらの仕事になる。
    //
    // 引数の並びはImGuiの描画リストに合わせてある（四角は左上と右下、円は中心と半径）。
    // 2Dで作りかけのものを持ってくるときに、呼び先を替えるだけで済むようにするため。
    //
    // どれも「世界の座標」で渡す。カメラ（Camera2D()）が画面のどこへ出るかを決める。
    // HUDのようにカメラを通したくないものは、Hud〜 の付いた方を使う
    // ========================================================================

    // 論理解像度。ゲームはこの大きさの画面に描くつもりで座標を書く。
    // 実際の窓がいくら大きくても、同じ絵が出る（縦横比が違うぶんは帯になる）
    Canvas2D& Get2DCanvas() { return canvas2D_; }
    Camera2D& Get2DCamera() { return camera2D_; }

    // ----- 四角（ImGuiのAddRectFilledと同じ引数） -----
    void Rect2D(float minX, float minY, float maxX, float maxY, const Vector4& color) {
        RectCentered2D((minX + maxX) * 0.5f, (minY + maxY) * 0.5f,
                       maxX - minX, maxY - minY, 0.0f, color);
    }

    // 中心と大きさで描く。傾けられる
    void RectCentered2D(float centerX, float centerY, float width, float height,
                        float rotation, const Vector4& color) {
        Push2D(Draw2DCommand::Kind::Quad, { centerX, centerY }, width, height, rotation, color, true);
    }

    // ----- 円（ImGuiのAddCircleFilledと同じ引数） -----
    void Circle2D(float centerX, float centerY, float radius, const Vector4& color) {
        Push2D(Draw2DCommand::Kind::Circle, { centerX, centerY }, radius * 2.0f, radius * 2.0f, 0.0f, color, true);
    }

    // ----- 線（ImGuiのAddLineと同じ引数＋太さ） -----
    // 細長い四角を、線の向きへ傾けて置く。
    // 線を引くパイプラインもあるが、そちらは太さを指定できない
    //（D3D12の線は必ず1px）ので、太い線が引けない
    void Line2D(float x0, float y0, float x1, float y1, float thickness, const Vector4& color) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 0.0001f) { return; }
        Push2D(Draw2DCommand::Kind::Quad, { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f },
               length, thickness, atan2f(dy, dx), color, true);
    }

    // ----- カメラを通さないもの（HUD） -----
    void HudRect2D(float minX, float minY, float maxX, float maxY, const Vector4& color) {
        Push2D(Draw2DCommand::Kind::Quad, { (minX + maxX) * 0.5f, (minY + maxY) * 0.5f },
               maxX - minX, maxY - minY, 0.0f, color, false);
    }
    void HudCircle2D(float centerX, float centerY, float radius, const Vector4& color) {
        Push2D(Draw2DCommand::Kind::Circle, { centerX, centerY }, radius * 2.0f, radius * 2.0f, 0.0f, color, false);
    }

    // ----- 絵を1枚置く（イラスト） -----
    //
    // パスは実行時のフォルダから見た場所（例 "resources/player.png"）。同じパスを何度渡しても読み込みは1回だけ
    //（2回目からは覚えてあるものを使う）。
    //
    // 大きさを渡さなければ、画像そのままの大きさで出る。
    // 480x270の絵を正方形の板に貼ると潰れるので、既定では縦横比を画像から取る。
    //
    // colorは掛け算。白なら元の絵のまま、薄くすれば透け、色を掛ければ染まる
    void Sprite2D(const std::string& texturePath, float centerX, float centerY,
                  float rotation = 0.0f, const Vector4& color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
                  float scale = 1.0f) {
        const Loaded2DTexture* texture = GetOr2DLoadTexture(texturePath);
        if (!texture) { return; }
        PushSprite2D(texture, { centerX, centerY },
                     texture->width * scale, texture->height * scale, rotation, color, true);
    }

    // 大きさを自分で決める版（引き伸ばしたいとき）
    void Sprite2DSized(const std::string& texturePath, float centerX, float centerY,
                       float width, float height, float rotation = 0.0f,
                       const Vector4& color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }) {
        const Loaded2DTexture* texture = GetOr2DLoadTexture(texturePath);
        if (!texture) { return; }
        PushSprite2D(texture, { centerX, centerY }, width, height, rotation, color, true);
    }

    // カメラを通さない版（HUDの絵）
    void HudSprite2D(const std::string& texturePath, float centerX, float centerY,
                     float rotation = 0.0f, const Vector4& color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f },
                     float scale = 1.0f) {
        const Loaded2DTexture* texture = GetOr2DLoadTexture(texturePath);
        if (!texture) { return; }
        PushSprite2D(texture, { centerX, centerY },
                     texture->width * scale, texture->height * scale, rotation, color, false);
    }

    // 絵の大きさ（ピクセル）。置く前に大きさを知りたいときに使う。
    // 読めなければ 0,0 を返す
    Vector2 Sprite2DSize(const std::string& texturePath) {
        const Loaded2DTexture* texture = GetOr2DLoadTexture(texturePath);
        return texture ? Vector2{ texture->width, texture->height } : Vector2{ 0.0f, 0.0f };
    }

    // ===== 部品（コンポーネント）からエンジンを呼ぶ窓口 =====
    // ComponentServicesの実装。部品はEngineの中身を知らず、ここに並んだ数個だけを頼れる。
    //
    // 窓口を細く保つのは意図的。ここへ何でも足していくと、
    // 結局「部品からエンジンの全部が触れる」ことになり、分けた意味がなくなる
    void PlayCue(const std::string& cue, float volume) override {
        // 名前でそのまま引く。どんな名前があるかはゲームがSoundBankへ登録したぶんだけなので、
        // エンジンは音の一覧を一切知らないままでいられる。
        // 登録されていない名前なら何も起きない（名前を打ち間違えても止まらない）
        sounds_.Play(cue, volume);
    }

    // 窓が閉じられるまで、入力更新〜描画を繰り返す。main.cppが呼ぶ
    void Run() {
        // 物理は固定タイムステップで回す。可変のdeltaTimeをそのまま積分すると、
        // フレームレートが変わるだけで跳ね方・止まる位置が変わってしまう
        const float kFixedTimeStep = 1.0f / 60.0f;
        // 1フレームで消化する物理時間の上限。処理落ちで溜まった時間を全部消化しようとすると
        // さらに処理落ちする悪循環になるので、ここで打ち切ってスローモーションに逃がす
        const float kMaxAccumulatedTime = 0.20f;
        float physicsAccumulator = 0.0f;

        // 前回デバッグビューで変えた内容を、コードで作った物へ重ねる（SetupSceneのあと）
        particles_.LoadSaved();
        savedEdits_ = LoadObjectEdits();
        {
            const std::vector<std::string> keys = MakeObjectEditKeys(objects_);
            for (size_t i = 0; i < objects_.size(); ++i) {
                auto it = savedEdits_.find(keys[i]);
                if (it != savedEdits_.end()) { it->second.ApplyTo(objects_[i]); }
            }
        }

        auto lastTime = std::chrono::steady_clock::now();
        while (ProcessWindowMessages()) {
            auto now = std::chrono::steady_clock::now();
            float frameTime = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            // ウィンドウ移動中などに長く止まったぶんは切り捨てる（復帰した瞬間に一気に進めない）
            frameTime = (std::min)(frameTime, kMaxAccumulatedTime);
            lastFrameTime_ = frameTime;
            totalTime_ += frameTime;
            float scaledTime = frameTime * timeScale_;

            input_.Update();
            audio_.Update();     // 鳴り終わった効果音の後始末（溜め続けると鳴らなくなる）
            if (screenshotKeyEnabled_ && input_.IsWindowActive() && input_.IsKeyTriggered(DIK_F12)) {
                RequestScreenshot();
            }
            // 全画面の出し入れ（F11 / Alt+Enter）
            if (input_.IsWindowActive()) {
                bool altEnter = input_.IsKeyTriggered(DIK_RETURN) && (input_.IsKeyDown(DIK_LMENU) || input_.IsKeyDown(DIK_RMENU));
                if (input_.IsKeyTriggered(DIK_F11) || altEnter) { ToggleFullscreen(); }
            }
            camera2D_.Update(frameTime); // 2Dカメラの揺れを減らす

            // --- デバッグビュー（F2）の出し入れ ---
            if (debugViewEnabled_ && input_.IsWindowActive() && input_.IsKeyTriggered(DIK_F2)) {
                SetDebugViewActive(!debugViewActive_);
            }

            if (debugViewActive_) {
                // デバッグビューの間はゲームを止める（物理・部品・SetUpdateCallbackを呼ばない）。
                // 動かしている最中の物を、物理やゲームのコードが引き戻さないようにするため
                physicsAccumulator = 0.0f;
                UpdateDebugView(frameTime);
                // 粒だけは動かす。止めてしまうと、設定を変えても結果が見えない
                particles_.Update(frameTime);
            } else {
                // --- 物理（固定ステップ）---
                physicsAccumulator = (std::min)(physicsAccumulator + scaledTime, kMaxAccumulatedTime);
                while (physicsAccumulator >= kFixedTimeStep) {
                    physics_.Step(kFixedTimeStep, objects_);
                    // イベントはStep()のたびに作り直されるので、次のStep()へ進む前に必ず配る
                    DispatchCollisionEvents();
                    physicsAccumulator -= kFixedTimeStep;
                }

                // --- 挿してある部品（物理の後。先に呼ぶと押し出しに上書きされる）---
                UpdateComponents(scaledTime);
                particles_.Update(scaledTime);

                // --- ゲームの処理（SetUpdateCallbackしたもの）---
                if (updateCallback_) { updateCallback_(frameTime); }
            }

            // --- 消す予約をまとめて実行 ---
            ApplyPendingDestroys();

            // --- カメラ確定 ---
            // ゲームがこのフレームで動かした位置から行列を作り直す（1フレーム遅れのカクつきを防ぐ）
            camera_.Update(frameTime);

            // デバッグビューの目印（選択枠・ギズモ・案内）は、カメラが確定してから積む
            if (debugViewActive_) { DrawDebugView(); }

#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            // デバッグビュー（F2）の間は、置いてある物の設定パネルを出す。
            // 選択はビューの中でクリックして選ぶものと共通
            if (debugViewActive_) {
                DrawObjectInspector(objects_, debugSelectedId_);
                DrawParticlePanel(particles_);
                // パネルの数値やギズモを触り終えた瞬間に保存する（終了し忘れ・異常終了でも消えないように）
                bool editing = ImGui::IsAnyItemActive() || gizmoDragAxis_ >= 0;
                if (debugWasEditing_ && !editing) { SaveDebugEdits(); }
                debugWasEditing_ = editing;
            }
            if (imguiCallback_) { imguiCallback_(frameTime); }
#endif

            RenderFrame();
        }
    }

    // 後始末。ComPtrで持つDirectXリソース自体はデストラクタで自動解放される
    void Shutdown() {
        if (!initialized_) { return; }
        if (debugViewActive_) { SaveDebugEdits(); } // デバッグビューのまま閉じられても保存する
        // GPUが使い終わってから片付ける（描画中のフォント等を消さないため）
#ifdef USE_IMGUI
        if (imguiInitialized_) { gfx_.WaitForGPU(); ShutdownImGui(); imguiInitialized_ = false; }
#endif
        sounds_.UnloadAll(audio_);
        audio_.Finalize();
        input_.Finalize();
        text_.Finalize(); // GDIのフォントハンドルを解放する
        if (gfx_.fenceEvent) { CloseHandle(gfx_.fenceEvent); gfx_.fenceEvent = nullptr; }
        CoUninitialize();
        initialized_ = false;
    }

    ~Engine() { Shutdown(); }

private:
    HWND hwnd_ = nullptr;
    bool fullscreen_ = false;
    RECT windowedRect_{};                 // 全画面にする前の窓の位置と大きさ
    bool hasWindowedRect_ = false;
    int32_t windowedClientWidth_ = 1280;
    int32_t windowedClientHeight_ = 720;

    // デバッグビューで変えた内容の保存
    std::map<std::string, ObjectEdit> savedEdits_;                // ファイルに書いてある内容（キーは名前#番号）
    std::unordered_map<uint32_t, ObjectEdit> debugSnapshot_;      // デバッグビューに入った時点（＝前回保存した時点）の値
    bool debugWasEditing_ = false;                                // 直前のフレームで、パネルかギズモを操作中だったか
    GraphicsContext gfx_;
    Object3DPipeline pipeline_;
    LinePipeline linePipeline_;                               // 当たり判定の枠（線）の描画用
    ShadowPipeline shadowPipeline_;                           // 地面へ落とす影の描画用（半透明合成つき）
    // 1フレームで描ける2Dの四角・円・絵の枚数（Rect2D / HudRect2D / Sprite2D …）
    static constexpr int kMaxDraw2DRects = 512;
    std::vector<LineTransformBuffer> draw2DTransformBuffers_;
    int draw2DDrawCount_ = 0;                                 // このフレームで何枚描いたか
    LineBoxMesh lineBoxMesh_;                                 // ユニットボックスの線
    // 読み込んだ絵1枚ぶん。大きさを覚えておくのは、画像そのままの縦横比で出せるようにするため
    struct Loaded2DTexture {
        ComPtr<ID3D12Resource> resource;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        float width = 0.0f;
        float height = 0.0f;
    };
    LinePipeline draw2DPipeline_;                             // 2D（図形と絵）用。奥行き無し・半透明あり
    Draw2DMesh draw2DQuadMesh_;                               // 2Dの四角1枚
    Draw2DMesh draw2DCircleMesh_;                             // 2Dの円1枚
    Draw2DMesh draw2DTriangleMesh_;                           // 2Dの三角1枚（+X向き。ギズモの矢じり）
    std::unordered_map<std::string, Loaded2DTexture> textures2D_; // パスから引く絵の置き場
    Canvas2D canvas2D_;                                       // 2Dの論理解像度
    Camera2D camera2D_;                                       // 2Dのカメラ（見る場所・寄り・揺れ）
    std::vector<Draw2DCommand> draw2DCommands_;               // このフレームに積まれた2Dの描画
    static constexpr size_t kMaxDraw2DCommands = 4096;        // 積める上限（描き過ぎで固まらないように）
    LineBoxMesh shadowQuadMesh_;                              // 影1つぶんの板（XZ平面の四角形）
    static constexpr int kMaxShadows = 128;                   // 同時に描ける影の数
    std::vector<LineTransformBuffer> shadowTransformBuffers_;
    float shadowOpacity_ = 0.38f;                             // 真下に接しているときの影の濃さ
    float shadowMaxHeight_ = 14.0f;                           // これより高いところにあると影が完全に消える
    static constexpr int kMaxColliderBoxes = 512;             // 同時に表示できる当たり判定の枠の数
    static constexpr int kLineTransformBufferCount = kMaxColliderBoxes;
    std::vector<LineTransformBuffer> lineTransformBuffers_;
    bool showColliders_ = false;                              // 当たり判定の枠を出すか（SetShowColliders）
    DefaultTextures textures_;
    DirectionalLightBuffer light_;
    TextRenderer text_;                                       // 画面への文字描画
    ParticleSystem particles_;                                // パーティクル（粒の管理・更新・描画）
    // 画面を撮る（F12）。枠なしの全画面はWindowsの撮影機能で撮れないことがあるので自前で撮る
    static constexpr const char* kScreenshotDirectory = "screenshots";
    bool screenshotRequested_ = false;
    bool screenshotKeyEnabled_ = true;
    ComPtr<ID3D12Resource> screenshotReadback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT screenshotFootprint_{};
    Camera camera_;
    AudioEngine audio_;
    SoundBank sounds_;                // 名前で引ける音の置き場（中身はゲームが登録する）
    InputEngine input_;
    std::vector<Object3D> objects_;
    std::unordered_map<std::string, CachedModelData> modelCache_; // キー："フォルダ/ファイル名"（複数部品なら末尾に"#部品名"）
    // "フォルダ/ファイル名" → そのファイルから作られた部品の見出し一覧
    std::unordered_map<std::string, std::vector<std::string>> modelPartKeys_;

    Vector4 clearColor_{ 0.10f, 0.25f, 0.50f, 1.0f }; // 何も描いていない所の色（SetClearColor）
    RenderFeatures features_;                          // どのシェーダーを通すか
    PhysicsWorld physics_;                             // 重力・当たり判定・押し出し（固定タイムステップで呼ぶ）

    float timeScale_ = 1.0f;      // 物理と部品にかける時間の倍率（SetTimeScale）
    float totalTime_ = 0.0f;      // 起動してからの通算[秒]
    float componentClock_ = 0.0f; // 部品へ渡す通算時間[秒]（時間の倍率がかかる）
    float lastFrameTime_ = 0.0f;  // 直前のフレームの経過時間[秒]

    std::vector<uint32_t> pendingDestroy_;  // Destroy() で予約されたid
    bool destroyAllRequested_ = false;       // DestroyAll() で予約された

    std::function<void(float)> imguiCallback_;                    // SetImGuiCallback
    bool imguiInitialized_ = false;
    std::function<void(float)> updateCallback_;                 // SetUpdateCallback
    std::function<void(const CollisionEvent&)> collisionCallback_; // SetCollisionCallback

    bool initialized_ = false;

    // ========================================================================
    // デバッグビュー（F2）の中身
    //
    // ギズモは画面に投影してから、2Dの太い線と三角で描く。
    // 3Dの線（1px）だと細くて掴みにくく、物に隠れると掴めなくなるため。
    // 当たり判定も同じ投影した形で行うので、見えている線をそのまま掴める。
    // 長さは画面に対して一定（Unityと同じ）。遠くの物でも近くの物でも同じ大きさで出る
    // ========================================================================
    enum class GizmoTool { Move, Rotate, Scale };

#ifdef _DEBUG
    bool debugViewEnabled_ = true;
#else
    bool debugViewEnabled_ = false;  // 配布する構成では出さない（SetDebugViewEnabledで変えられる）
#endif
    bool debugViewActive_ = false;
    bool debugCameraPlaced_ = false; // 最初に開いたときだけ、ゲームの視点から始める
    Camera debugCamera_;             // デバッグビュー用の視点（ゲームのカメラとは別）
    uint32_t debugSelectedId_ = 0;
    GizmoTool gizmoTool_ = GizmoTool::Move;
    int gizmoHoverAxis_ = -1;        // マウスが乗っている軸（-1:なし 0:X 1:Y 2:Z）
    int gizmoDragAxis_ = -1;         // 掴んでいる軸
    Vector3 dragStartTranslate_{};
    Vector3 dragStartRotate_{};
    Vector3 dragStartScale_{ 1.0f, 1.0f, 1.0f };
    float dragStartT_ = 0.0f;        // 掴んだ瞬間の、軸の上でマウスに一番近い点
    float dragArmLength_ = 1.0f;     // 掴んだ瞬間のギズモの長さ（拡縮中に長さが変わって暴れないよう固定する）
    float dragPrevAngle_ = 0.0f;     // 回転：前のフレームの輪の上の角度
    float dragAccumAngle_ = 0.0f;    // 回転：掴んでからの合計

    static constexpr float kGizmoPickPixels = 12.0f;  // 軸を掴める距離[px]
    static constexpr float kGizmoScreenRatio = 0.13f; // ギズモの長さ（画面の縦に対する割合）
    static constexpr float kSnapMove = 0.25f;         // Ctrlで刻むときの幅
    static constexpr float kSnapRotateDegrees = 15.0f;
    static constexpr float kSnapScale = 0.1f;

    // 今の画面をどちらのカメラで映しているか
    const Camera& ViewCamera() const { return debugViewActive_ ? debugCamera_ : camera_; }

    void SetDebugViewActive(bool active) {
        if (active == debugViewActive_) { return; }
        if (active) {
            // 入った時点の値を控えておく。出るとき・操作を終えたときに、ここから変わった物だけを保存する
            debugSnapshot_.clear();
            for (const Object3D& obj : objects_) { debugSnapshot_[obj.id] = ObjectEdit::From(obj); }
            particles_.BeginEditSession();
            debugWasEditing_ = false;
        } else {
            SaveDebugEdits();
        }
        debugViewActive_ = active;
        gizmoDragAxis_ = -1;
        gizmoHoverAxis_ = -1;
        if (active && !debugCameraPlaced_) {
            // 映し方も揃える（2Dのゲームなら平行投影のまま見る）
            debugCamera_.SetProjection(camera_.GetProjection());
            debugCamera_.SetOrthoHeight(camera_.GetOrthoHeight());
            debugCamera_.SetFovY(camera_.GetFovY());
            debugCamera_.SetNearFar(camera_.GetNearZ(), camera_.GetFarZ());
            debugCamera_.GetTransform() = camera_.GetTransform();
            debugCamera_.SyncMatrices();
            debugCameraPlaced_ = true;
        }
        Log(active ? "[Debug] デバッグビューに切り替えました（ゲームは止まっています。F2で戻る）"
                   : "[Debug] ゲームへ戻りました");
    }

    // デバッグビューに入った時点から変わった物を、保存ファイルへ書く。変わっていなければ何もしない。
    // 保存したら、その値を「入った時点」の値へ更新する（同じ内容を何度も書かないため）
    void SaveDebugEdits() {
        const std::vector<std::string> keys = MakeObjectEditKeys(objects_);
        bool changed = false;
        for (size_t i = 0; i < objects_.size(); ++i) {
            const ObjectEdit now = ObjectEdit::From(objects_[i]);
            auto snap = debugSnapshot_.find(objects_[i].id);
            if (snap != debugSnapshot_.end() && snap->second == now) { continue; }
            savedEdits_[keys[i]] = now;
            debugSnapshot_[objects_[i].id] = now;
            changed = true;
        }
        if (changed) { SaveObjectEdits(savedEdits_); }
        particles_.SaveEdits(); // パーティクルの設定も同じタイミングで保存する
    }

    void UpdateDebugView(float deltaTime) {
        debugCamera_.Update(deltaTime);

        Object3D* selected = FindObjectById(debugSelectedId_);
        if (!selected) { debugSelectedId_ = 0; }

        bool windowActive = input_.IsWindowActive();
        bool flying = input_.IsMouseDown(MouseButton::Right); // 右ボタン中のキーは視点の移動に使う

        // ImGuiのパネルを操作中は、マウスもキーもパネルのもの（数値を打った文字がツール切り替えに化けない／
        // パネルをクリックしても背後の物を選ばない）。掴んでいる最中は、途中で奪わない
        bool uiMouse = false, uiKeyboard = false;
#ifdef USE_IMGUI
        uiMouse = ImGui::GetIO().WantCaptureMouse && gizmoDragAxis_ < 0;
        uiKeyboard = ImGui::GetIO().WantCaptureKeyboard;
#endif

        // ツールの切り替え（UnityのW/E/R）と、選んだ物へ寄る（F）
        if (windowActive && !flying && gizmoDragAxis_ < 0 && !uiKeyboard) {
            if (input_.IsKeyTriggered(DIK_W)) { gizmoTool_ = GizmoTool::Move; }
            if (input_.IsKeyTriggered(DIK_E)) { gizmoTool_ = GizmoTool::Rotate; }
            if (input_.IsKeyTriggered(DIK_R)) { gizmoTool_ = GizmoTool::Scale; }
            if (input_.IsKeyTriggered(DIK_F) && selected) { FocusDebugCamera(*selected); }
        }

        Vector3 rayOrigin{}, rayDir{};
        bool hasRay = ComputeMouseRay(input_.MousePosition(), rayOrigin, rayDir);

        // 掴んでいる最中
        if (gizmoDragAxis_ >= 0) {
            if (!selected || !input_.IsMouseDown(MouseButton::Left)) { gizmoDragAxis_ = -1; return; }
            if (hasRay) { ApplyGizmoDrag(*selected, rayOrigin, rayDir); }
            return;
        }

        POINT mouse = input_.MousePosition();
        gizmoHoverAxis_ = (selected && !flying && windowActive && !uiMouse)
            ? PickGizmoAxis(*selected, { (float)mouse.x, (float)mouse.y }) : -1;

        if (windowActive && !flying && !uiMouse && input_.IsMouseTriggered(MouseButton::Left) && hasRay) {
            if (selected && gizmoHoverAxis_ >= 0) {
                BeginGizmoDrag(*selected, gizmoHoverAxis_, rayOrigin, rayDir);
            } else {
                // 何も無い所をクリックしたら選択を外す（Unityと同じ）
                debugSelectedId_ = PickObject(rayOrigin, rayDir);
            }
        }
    }

    // レイで一番手前の物を選ぶ（見えていないもの・スプライトは選ばない）
    uint32_t PickObject(const Vector3& rayOrigin, const Vector3& rayDir) {
        uint32_t best = 0;
        float bestDistance = (std::numeric_limits<float>::max)();
        for (const Object3D& obj : objects_) {
            if (!obj.visible || obj.isSprite) { continue; }
            float distance = 0.0f;
            if (RayIntersectOBB(rayOrigin, rayDir, obj.GetWorldOBB(), distance) && distance < bestDistance) {
                bestDistance = distance;
                best = obj.id;
            }
        }
        return best;
    }

    // 画面上で一定の長さに見えるよう、ギズモの長さ（ワールドの長さ）を決める
    float GizmoWorldLength(const Vector3& center) const {
        if (debugCamera_.GetProjection() == CameraProjection::Orthographic) {
            return debugCamera_.GetOrthoHeight() * kGizmoScreenRatio;
        }
        Vector3 eye = debugCamera_.GetPosition();
        Vector3 toCenter{ center.x - eye.x, center.y - eye.y, center.z - eye.z };
        float depth = (std::max)(0.1f, Gizmo::Dot(toCenter, debugCamera_.Forward()));
        return depth * tanf(debugCamera_.GetFovY() * 0.5f) * 2.0f * kGizmoScreenRatio;
    }

    // マウス（画面のピクセル）に一番近い軸。どれも遠ければ-1
    int PickGizmoAxis(const Object3D& obj, const Vector2& mouse) const {
        const Matrix4x4 vp = debugCamera_.GetViewProjectionMatrix();
        const float width = (float)gfx_.clientWidth;
        const float height = (float)gfx_.clientHeight;
        const Vector3& center = obj.transform.translate;
        const float length = GizmoWorldLength(center);

        int bestAxis = -1;
        float bestDistance = kGizmoPickPixels;
        if (gizmoTool_ == GizmoTool::Rotate) {
            constexpr int kSamples = 48;
            for (int a = 0; a < 3; ++a) {
                Vector2 prev{};
                bool prevValid = false;
                for (int i = 0; i <= kSamples; ++i) {
                    float angle = 6.28318530718f * (float)i / (float)kSamples;
                    Vector2 screen{};
                    bool valid = Gizmo::ProjectToScreen(Gizmo::PointOnRing(center, a, length, angle), vp, width, height, screen);
                    if (valid && prevValid) {
                        float d = Gizmo::DistancePointToSegment(mouse, prev, screen);
                        if (d < bestDistance) { bestDistance = d; bestAxis = a; }
                    }
                    prev = screen;
                    prevValid = valid;
                }
            }
            return bestAxis;
        }

        Vector2 origin{};
        if (!Gizmo::ProjectToScreen(center, vp, width, height, origin)) { return -1; }
        for (int a = 0; a < 3; ++a) {
            Vector2 tip{};
            if (!Gizmo::ProjectToScreen(Gizmo::AddScaled(center, Gizmo::kAxis[a], length), vp, width, height, tip)) { continue; }
            float d = Gizmo::DistancePointToSegment(mouse, origin, tip);
            if (d < bestDistance) { bestDistance = d; bestAxis = a; }
        }
        return bestAxis;
    }

    void BeginGizmoDrag(Object3D& obj, int axis, const Vector3& rayOrigin, const Vector3& rayDir) {
        const Vector3& center = obj.transform.translate;
        dragStartTranslate_ = obj.transform.translate;
        dragStartRotate_ = obj.transform.rotate;
        dragStartScale_ = obj.transform.scale;
        dragArmLength_ = GizmoWorldLength(center);

        if (gizmoTool_ == GizmoTool::Rotate) {
            Vector3 hit{};
            // 輪を真横から見ていると交点が求まらない。そのときは掴めなかったことにする
            if (!Gizmo::RayPlaneIntersect(rayOrigin, rayDir, center, Gizmo::kAxis[axis], hit)) { return; }
            dragPrevAngle_ = Gizmo::AngleOnRing(center, axis, hit);
            dragAccumAngle_ = 0.0f;
        } else {
            dragStartT_ = Gizmo::ClosestTOnAxis(center, Gizmo::kAxis[axis], rayOrigin, rayDir);
        }
        gizmoDragAxis_ = axis;
    }

    static float& AxisComponent(Vector3& v, int axis) { return (axis == 0) ? v.x : (axis == 1) ? v.y : v.z; }

    // 掴んだ時点の値を基準に、毎フレーム作り直す（少しずつ足す方式だと誤差が溜まり、刻みも効かない）
    void ApplyGizmoDrag(Object3D& obj, const Vector3& rayOrigin, const Vector3& rayDir) {
        const int axis = gizmoDragAxis_;
        const Vector3& dir = Gizmo::kAxis[axis];
        bool snap = input_.IsKeyDown(DIK_LCONTROL) || input_.IsKeyDown(DIK_RCONTROL);

        switch (gizmoTool_) {
        case GizmoTool::Move: {
            float delta = Gizmo::ClosestTOnAxis(dragStartTranslate_, dir, rayOrigin, rayDir) - dragStartT_;
            Vector3 moved = Gizmo::AddScaled(dragStartTranslate_, dir, delta);
            if (snap) { AxisComponent(moved, axis) = Gizmo::SnapTo(AxisComponent(moved, axis), kSnapMove); }
            obj.transform.translate = moved;
            break;
        }
        case GizmoTool::Rotate: {
            Vector3 hit{};
            if (!Gizmo::RayPlaneIntersect(rayOrigin, rayDir, dragStartTranslate_, dir, hit)) { break; }
            // 1フレームぶんの差を積む。角度そのものの差だと、±180度をまたいだ瞬間に1回転ぶん飛ぶ
            float angle = Gizmo::AngleOnRing(dragStartTranslate_, axis, hit);
            dragAccumAngle_ += Gizmo::WrapAngle(angle - dragPrevAngle_);
            dragPrevAngle_ = angle;
            Vector3 rotated = dragStartRotate_;
            AxisComponent(rotated, axis) += dragAccumAngle_;
            if (snap) {
                constexpr float kDegToRad = 3.14159265359f / 180.0f;
                AxisComponent(rotated, axis) = Gizmo::SnapTo(AxisComponent(rotated, axis), kSnapRotateDegrees * kDegToRad);
            }
            // 姿勢の実体はクォータニオンだが、物理は再開したときにオイラー角の変化を取り込む
            obj.transform.rotate = rotated;
            break;
        }
        case GizmoTool::Scale: {
            // 掴んだ手をギズモ1本ぶん外へ引いたら2倍、という割合で決める
            float delta = Gizmo::ClosestTOnAxis(dragStartTranslate_, dir, rayOrigin, rayDir) - dragStartT_;
            float factor = 1.0f + delta / (std::max)(0.01f, dragArmLength_);
            factor = (std::max)(0.05f, (std::min)(20.0f, factor));
            Vector3 scaled = dragStartScale_;
            AxisComponent(scaled, axis) *= factor;
            if (snap) {
                AxisComponent(scaled, axis) = (std::max)(kSnapScale, Gizmo::SnapTo(AxisComponent(scaled, axis), kSnapScale));
            } else {
                AxisComponent(scaled, axis) = (std::max)(0.01f, AxisComponent(scaled, axis));
            }
            obj.transform.scale = scaled;
            break;
        }
        }

        // ゲームへ戻った瞬間に、前の勢いで飛んでいかないようにする。眠っていたら起こす
        obj.velocity = { 0.0f, 0.0f, 0.0f };
        obj.angularVelocity = { 0.0f, 0.0f, 0.0f };
        obj.sleepTimer = 0.0f;
    }

    // 選んだ物が画面に収まる距離へ寄る。向きは変えない（見ていた角度が変わると迷子になる）
    void FocusDebugCamera(const Object3D& obj) {
        OBB obb = obj.GetWorldOBB();
        float radius = sqrtf(Gizmo::Dot(obb.halfSize, obb.halfSize));
        float distance = (std::max)(2.0f, radius * 3.0f);
        Vector3 forward = debugCamera_.Forward();
        debugCamera_.SetPosition(Gizmo::AddScaled(obb.center, forward, -distance));
    }

    // ----- 目印を積む（2DのHUDとして、3Dの上に出る）-----

    Vector2 PixelToCanvas(const Vector2& pixel, const Canvas2D::Fit& fit) const {
        return { (pixel.x - fit.offsetX) / fit.scale, (pixel.y - fit.offsetY) / fit.scale };
    }

    void HudLine(const Vector2& a, const Vector2& b, float thickness, const Vector4& color) {
        float dx = b.x - a.x, dy = b.y - a.y;
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 0.001f) { return; }
        Push2D(Draw2DCommand::Kind::Quad, { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f }, length, thickness,
               atan2f(dy, dx), color, false);
    }

    void DrawDebugView() {
        const Canvas2D::Fit fit = canvas2D_.MakeFit((float)gfx_.clientWidth, (float)gfx_.clientHeight);
        const Matrix4x4 vp = debugCamera_.GetViewProjectionMatrix();
        const float width = (float)gfx_.clientWidth;
        const float height = (float)gfx_.clientHeight;
        auto Project = [&](const Vector3& world, Vector2& outCanvas) {
            Vector2 pixel{};
            if (!Gizmo::ProjectToScreen(world, vp, width, height, pixel)) { return false; }
            outCanvas = PixelToCanvas(pixel, fit);
            return true;
        };

        // 画面の縁に色を付けて、今がデバッグビューだと一目で分かるようにする
        const Vector4 kFrame{ 1.0f, 0.55f, 0.1f, 0.85f };
        const float w = canvas2D_.width, h = canvas2D_.height, t = 4.0f;
        HudRect2D(0.0f, 0.0f, w, t, kFrame);
        HudRect2D(0.0f, h - t, w, h, kFrame);
        HudRect2D(0.0f, 0.0f, t, h, kFrame);
        HudRect2D(w - t, 0.0f, w, h, kFrame);

        Object3D* selected = FindObjectById(debugSelectedId_);
        if (selected) {
            // --- 選択枠（当たり判定の箱の12辺）---
            OBB obb = selected->GetWorldOBB();
            Vector3 corners[8];
            for (int i = 0; i < 8; ++i) {
                float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f, sz = (i & 4) ? 1.0f : -1.0f;
                Vector3 c = obb.center;
                c = Gizmo::AddScaled(c, obb.axis[0], sx * obb.halfSize.x);
                c = Gizmo::AddScaled(c, obb.axis[1], sy * obb.halfSize.y);
                c = Gizmo::AddScaled(c, obb.axis[2], sz * obb.halfSize.z);
                corners[i] = c;
            }
            static constexpr int kEdges[12][2] = {
                {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
            const Vector4 kOutline{ 1.0f, 0.6f, 0.1f, 1.0f };
            for (const auto& edge : kEdges) {
                Vector2 a{}, b{};
                if (Project(corners[edge[0]], a) && Project(corners[edge[1]], b)) { HudLine(a, b, 1.5f, kOutline); }
            }

            // --- ギズモ ---
            const Vector4 kAxisColor[3] = { { 0.93f, 0.25f, 0.25f, 1.0f },   // X 赤
                                            { 0.45f, 0.85f, 0.25f, 1.0f },   // Y 緑
                                            { 0.25f, 0.50f, 1.00f, 1.0f } }; // Z 青
            const Vector4 kActive{ 1.0f, 0.9f, 0.2f, 1.0f };
            int highlighted = (gizmoDragAxis_ >= 0) ? gizmoDragAxis_ : gizmoHoverAxis_;
            const Vector3 center = selected->transform.translate;
            const float length = (gizmoDragAxis_ >= 0 && gizmoTool_ == GizmoTool::Scale) ? dragArmLength_ : GizmoWorldLength(center);

            Vector2 origin{};
            bool originVisible = Project(center, origin);
            for (int a = 0; a < 3; ++a) {
                const Vector4& color = (a == highlighted) ? kActive : kAxisColor[a];
                if (gizmoTool_ == GizmoTool::Rotate) {
                    constexpr int kSegments = 64;
                    Vector2 prev{};
                    bool prevValid = false;
                    for (int i = 0; i <= kSegments; ++i) {
                        float angle = 6.28318530718f * (float)i / (float)kSegments;
                        Vector2 p{};
                        bool valid = Project(Gizmo::PointOnRing(center, a, length, angle), p);
                        if (valid && prevValid) { HudLine(prev, p, 2.5f, color); }
                        prev = p;
                        prevValid = valid;
                    }
                    continue;
                }
                if (!originVisible) { continue; }
                // 拡縮中は、その軸だけ伸び縮みして見せる
                float armLength = length;
                if (gizmoTool_ == GizmoTool::Scale && a == gizmoDragAxis_ && AxisComponent(dragStartScale_, a) > 0.0f) {
                    armLength *= AxisComponent(selected->transform.scale, a) / AxisComponent(dragStartScale_, a);
                }
                Vector2 tip{};
                if (!Project(Gizmo::AddScaled(center, Gizmo::kAxis[a], armLength), tip)) { continue; }
                float dx = tip.x - origin.x, dy = tip.y - origin.y;
                float screenLength = sqrtf(dx * dx + dy * dy);
                HudLine(origin, tip, 3.0f, color);
                if (screenLength < 4.0f) { continue; } // 軸を真正面から見ている：向きが決まらない
                float rotation = atan2f(dy, dx);
                if (gizmoTool_ == GizmoTool::Move) {
                    // 矢じり（先端から少し先まで被せる）
                    Vector2 head{ tip.x + dx / screenLength * 4.0f, tip.y + dy / screenLength * 4.0f };
                    Push2D(Draw2DCommand::Kind::Triangle, head, 18.0f, 18.0f, rotation, color, false);
                } else {
                    // 拡縮は先端に四角（Unityと同じ）
                    Push2D(Draw2DCommand::Kind::Quad, tip, 11.0f, 11.0f, rotation, color, false);
                }
            }
            if (originVisible && gizmoTool_ != GizmoTool::Rotate) {
                Push2D(Draw2DCommand::Kind::Circle, origin, 7.0f, 7.0f, 0.0f, { 1.0f, 1.0f, 1.0f, 0.9f }, false);
            }
        }

        // --- 案内の文字 ---
        const Vector4 kTitle{ 1.0f, 0.7f, 0.3f, 1.0f };
        const Vector4 kDim{ 0.85f, 0.85f, 0.85f, 1.0f };
        const char* toolName = (gizmoTool_ == GizmoTool::Move) ? "移動" : (gizmoTool_ == GizmoTool::Rotate) ? "回転" : "拡縮";
        float x = kUiMarginX, y = kUiMarginY;
        DrawText(x, y, "デバッグビュー", 22.0f, kTitle);
        y += UiLine(22.0f);
        DrawText(x, y, std::string("ツール：") + toolName + "　[W]移動 [E]回転 [R]拡縮", 18.0f, kDim);
        y += UiLine(18.0f);
        DrawText(x, y, "左クリック：選ぶ　右ドラッグ＋WASD/QE：視点　F：寄る　Ctrl：刻む", 16.0f, kDim);

        if (selected) {
            constexpr float kRadToDeg = 57.2957795f;
            const Transform& tr = selected->transform;
            float by = kUiHeight - kUiMarginY - UiLine(18.0f) * 4.0f;
            DrawText(x, by, selected->name + (selected->tag.empty() ? "" : "  [" + selected->tag + "]") +
                            "  id " + std::to_string(selected->id), 20.0f, kTitle);
            by += UiLine(18.0f) * 1.1f;
            DrawText(x, by, std::format("位置   {:8.2f} {:8.2f} {:8.2f}", tr.translate.x, tr.translate.y, tr.translate.z), 18.0f);
            by += UiLine(18.0f);
            DrawText(x, by, std::format("回転°  {:8.1f} {:8.1f} {:8.1f}", tr.rotate.x * kRadToDeg, tr.rotate.y * kRadToDeg, tr.rotate.z * kRadToDeg), 18.0f);
            by += UiLine(18.0f);
            DrawText(x, by, std::format("大きさ {:8.2f} {:8.2f} {:8.2f}", tr.scale.x, tr.scale.y, tr.scale.z), 18.0f);
        }
    }

    // 全オブジェクトの部品を1回ずつ更新する。
    // 添字で回すのは、部品の中からオブジェクトが増えても手元が無効にならないようにするため
    void UpdateComponents(float deltaTime) {
        componentClock_ += deltaTime;
        for (size_t i = 0; i < objects_.size(); ++i) {
            Object3D& obj = objects_[i];
            if (obj.components.Empty()) { continue; }
            ComponentContext ctx{ obj, deltaTime, componentClock_, this };
            obj.components.Update(ctx);
        }
    }

    // Destroy() で予約されたものを消す。フレームの区切りでまとめて行うのは、
    // 衝突の通知や部品の更新の最中に消すと、配っている最中の相手が消えた物を指すことになるため
    void ApplyPendingDestroys() {
        if (destroyAllRequested_) {
            objects_.clear();
            physics_.ClearContactHistory();
            destroyAllRequested_ = false;
            pendingDestroy_.clear();
            return;
        }
        if (pendingDestroy_.empty()) { return; }
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object3D& obj) {
            return std::find(pendingDestroy_.begin(), pendingDestroy_.end(), obj.id) != pendingDestroy_.end();
        }), objects_.end());
        pendingDestroy_.clear();
        // 消えた相手との「触れていた」記録を残すと、次のステップで消えた物のExitが飛ぶ
        physics_.ClearContactHistory();
    }

    // 物理が作った当たりを、ゲームと、挿してある部品の両方へ配る
    void DispatchCollisionEvents() {
        for (const CollisionEvent& e : physics_.Events()) {
            if (collisionCallback_) { collisionCallback_(e); }
            DispatchToOne(e, e.a, e.b, false);
            DispatchToOne(e, e.b, e.a, true);  // b側は法線を反転して渡す（部品は常に「自分」基準で受け取る）
        }
    }

    void DispatchToOne(const CollisionEvent& e, Object3D* self, Object3D* other, bool flipNormal) {
        if (!self || self->components.Empty()) { return; }
        ComponentCollision hit;
        hit.other = other;
        hit.phase = e.phase;
        hit.isTrigger = e.isTrigger;
        hit.point = e.point;
        hit.normal = flipNormal ? Vector3{ -e.normal.x, -e.normal.y, -e.normal.z } : e.normal;
        hit.impulse = e.impulse;
        hit.approachSpeed = e.approachSpeed;

        ComponentContext ctx{ *self, lastFrameTime_ * timeScale_, componentClock_, this };
        self->components.Collide(ctx, hit);
    }

    // IDから今の配列位置を引く。見つからなければ-1
    int IndexOfObjectId(uint32_t id) const {
        if (id == 0) { return -1; }
        for (int i = 0; i < (int)objects_.size(); ++i) {
            if (objects_[i].id == id) { return i; }
        }
        return -1;
    }
    Object3D* FindObjectById(uint32_t id) {
        int index = IndexOfObjectId(id);
        return (index >= 0) ? &objects_[index] : nullptr;
    }

    // 画面のピクセル位置を通る、カメラからのレイ（ワールド空間）
    bool ComputeMouseRay(POINT pt, Vector3& outOrigin, Vector3& outDir) {
        if (pt.x < 0 || pt.y < 0 || pt.x >= gfx_.clientWidth || pt.y >= gfx_.clientHeight) { return false; }

        float ndcX = (2.0f * (float)pt.x) / (float)gfx_.clientWidth - 1.0f;
        float ndcY = 1.0f - (2.0f * (float)pt.y) / (float)gfx_.clientHeight; // スクリーンはY下向き、NDCはY上向き

        Matrix4x4 invVP = Inverse4x4(ViewCamera().GetViewProjectionMatrix());
        Vector3 nearPoint = UnprojectNDCToWorld(ndcX, ndcY, 0.0f, invVP);
        Vector3 farPoint = UnprojectNDCToWorld(ndcX, ndcY, 1.0f, invVP);

        // 始点は近い面の上の点にする（平行投影でもカメラの位置から出ると、横にずれた所を指せない）
        outOrigin = nearPoint;
        Vector3 dirRaw = { farPoint.x - nearPoint.x, farPoint.y - nearPoint.y, farPoint.z - nearPoint.z };
        float len = sqrtf(dirRaw.x * dirRaw.x + dirRaw.y * dirRaw.y + dirRaw.z * dirRaw.z);
        if (len < 1e-6f) { return false; }
        outDir = { dirRaw.x / len, dirRaw.y / len, dirRaw.z / len };
        return true;
    }

    // directoryPath+filenameのモデルが未読み込みならファイルから読み込んでキャッシュに積み、
    // 部品ごとのキャッシュの見出しを順に返す（AddModelの入口）。
    // 見出しは、部品が1つなら "フォルダ/ファイル名"、複数なら "フォルダ/ファイル名#部品名"
    std::vector<std::string> GetOrLoadCachedModelKeys(const std::string& directoryPath, const std::string& filename) {
        std::string fileKey = directoryPath + "/" + filename;
        auto cachedKeys = modelPartKeys_.find(fileKey);
        if (cachedKeys != modelPartKeys_.end()) { return cachedKeys->second; }

        // テクスチャの置き場所（SRVヒープ）が満杯なら、専用テクスチャの読み込みだけ諦める。
        // 形は既定のテクスチャで出るので作業は続けられる。
        // 確かめずに読むとヒープの外へ書き込むことになり、描画全体が壊れる
        bool srvSlotAvailable = gfx_.nextSrvIndex < GraphicsContext::kSrvDescriptorCount;
        if (!srvSlotAvailable) {
            Log(std::format("[Model] テクスチャの置き場所が上限({}枚)に達しました。\"{}\"は既定のテクスチャで表示します",
                            GraphicsContext::kSrvDescriptorCount, fileKey));
        }
        std::vector<CachedModelPart> parts = LoadCachedModelParts(
            gfx_.device.Get(), gfx_.commandList.Get(), gfx_.srvDescriptorHeap.Get(), gfx_.descriptorSizeSRV, gfx_.nextSrvIndex,
            gfx_.pendingIntermediateResources, directoryPath, filename, srvSlotAvailable);
        // テクスチャはファイルにつき1枚を全部品で共有しているので、進めるスロットも1つだけ
        if (!parts.empty() && parts.front().data.hasOwnTexture) { gfx_.nextSrvIndex++; }

        std::vector<std::string> keys;
        keys.reserve(parts.size());
        for (CachedModelPart& part : parts) {
            std::string key = part.name.empty() ? fileKey : (fileKey + "#" + part.name);
            modelCache_[key] = std::move(part.data);
            keys.push_back(std::move(key));
        }
        modelPartKeys_.emplace(fileKey, keys);
        return keys;
    }

    // キャッシュ済みの見出しからオブジェクトを1つ置き、そのIDを返す（見つからなければ0）
    uint32_t AddCachedModelByKey(const std::string& key) {
        auto it = modelCache_.find(key);
        if (it == modelCache_.end()) { return 0; }
        // 名前からはフォルダを落とす
        size_t slash = key.find_last_of('/');
        std::string displayName = (slash == std::string::npos) ? key : key.substr(slash + 1);
        objects_.push_back(CreateObject3DFromCachedModel(gfx_.device.Get(), it->second, displayName));
        objects_.back().sourceModel = key; // どのファイルの部品から来たか
        return objects_.back().id;
    }

    // メッセージポンプ。WM_QUITを受け取ったらfalseを返す
    bool ProcessWindowMessages() {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { return false; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }

    // 1フレーム分の描画（クリア〜Present〜GPU待機まで）
    void RenderFrame() {
        UINT backBufferIndex = gfx_.swapChain->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = gfx_.swapChainResources[backBufferIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        gfx_.commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = gfx_.dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = gfx_.rtvHandles[backBufferIndex];

        gfx_.commandList->OMSetRenderTargets(1, &sceneRtv, false, &dsvHandle);
        // 何も描いていない所の色。ゲームから変えられる（SetClearColor）
        gfx_.commandList->ClearRenderTargetView(sceneRtv, &clearColor_.x, 0, nullptr);
        gfx_.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        ID3D12DescriptorHeap* heaps[] = { gfx_.srvDescriptorHeap.Get() };
        gfx_.commandList->SetDescriptorHeaps(1, heaps);
        gfx_.commandList->RSSetViewports(1, &gfx_.viewport);
        gfx_.commandList->RSSetScissorRects(1, &gfx_.scissorRect);
        RestoreObjectPipeline();

        Matrix4x4 cameraVP = ViewCamera().GetViewProjectionMatrix(); // デバッグビュー中はデバッグ用カメラから見る
        Matrix4x4 spriteVP = MakeOrthographicMatrix(0.0f, 0.0f, (float)gfx_.clientWidth, (float)gfx_.clientHeight, 0.0f, 100.0f);

        // 2Dの四角の枚数は、フレームの頭で数え直す
        draw2DDrawCount_ = 0;

        // 不透明な物を先に描き、半透明な物は遠い順に重ねる（近い物から描くと、後ろの物が透けて見えない）
        std::vector<Object3D*> blended;
        for (auto& obj : objects_) {
            if (!obj.visible) { continue; }
            UpdateObjectMatrices(obj, cameraVP, spriteVP);
            if (obj.blendMode != BlendMode::kNone) { blended.push_back(&obj); continue; }
            DrawObject(gfx_.commandList.Get(), obj, textures_);
        }
        if (!blended.empty()) {
            const Vector3 cam = ViewCamera().GetPosition();
            auto distSq = [&cam](const Object3D* o) {
                const Vector3& t = o->transform.translate;
                return (t.x - cam.x) * (t.x - cam.x) + (t.y - cam.y) * (t.y - cam.y) + (t.z - cam.z) * (t.z - cam.z);
            };
            std::stable_sort(blended.begin(), blended.end(),
                [&](const Object3D* a, const Object3D* b) { return distSq(a) > distSq(b); });
            for (Object3D* obj : blended) {
                // 先に奥行きだけを描き、そのあと色を合成する
                gfx_.commandList->SetPipelineState(pipeline_.depthOnlyPso.Get());
                DrawObject(gfx_.commandList.Get(), *obj, textures_);
                gfx_.commandList->SetPipelineState(pipeline_.psos[(int)obj->blendMode].Get());
                DrawObject(gfx_.commandList.Get(), *obj, textures_);
            }
            gfx_.commandList->SetPipelineState(pipeline_.pso.Get());
        }

        // 影は不透明な物をすべて描いたあとに重ねる。半透明なので、先に描くと
        // 後から描いた地面に塗りつぶされてしまう
        DrawShadows(cameraVP);

        // パーティクルは半透明なので、不透明な物と影を描いたあとに重ねる
        {
            const Camera& viewCamera = ViewCamera();
            particles_.Draw(gfx_.commandList.Get(), cameraVP, viewCamera.Right(), viewCamera.Up(), viewCamera.Forward(),
                            textures_.whiteSrv, textures_.uvCheckerSrv);
            RestoreObjectPipeline(); // 別のルートシグネチャを使ったので、次に描く物のために戻す
        }

        if (showColliders_) {
            DrawColliders(cameraVP, spriteVP);
        }

        // 2Dの図形と絵は3Dの上、文字の下
        if (!draw2DCommands_.empty()) {
            FlushDraw2D();
            RestoreObjectPipeline();
        }

        // 画面表示（文字）は最後にまとめて描く。3Dの上に重ねるものなので、
        // ここまでの描画がすべて終わってからでないと下に隠れてしまう。
        // 何を出すかはゲームが決める（DrawText / DrawTextCentered をSetUpdateCallbackの中で呼ぶ）
        text_.Flush(gfx_.commandList.Get(), gfx_.srvDescriptorHeap.Get(), gfx_.clientWidth, gfx_.clientHeight);

#ifdef USE_IMGUI
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gfx_.commandList.Get());
#endif

        // 撮るのはここ。実際に出ている絵をそのまま読み戻す
        if (screenshotRequested_) { screenshotRequested_ = false; CopyBackBufferForScreenshot(backBufferIndex); }

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        gfx_.commandList->ResourceBarrier(1, &barrier);
        gfx_.commandList->Close();

        ID3D12CommandList* lists[] = { gfx_.commandList.Get() };
        gfx_.commandQueue->ExecuteCommandLists(1, lists);
        gfx_.swapChain->Present(1, 0);

        gfx_.WaitForGPU();
        // GPUが写し終えた後でないと読めない
        SaveScreenshotFile();
        gfx_.commandAllocator->Reset();
        gfx_.commandList->Reset(gfx_.commandAllocator.Get(), nullptr);

        // OBJ読み込みでテクスチャをアップロードしていた場合、GPU転送完了後のここで中間リソースを解放する
        if (!gfx_.pendingIntermediateResources.empty()) { gfx_.pendingIntermediateResources.clear(); }
    }

    // ===== 画面を撮る（F12）=====
    // Windowsの Win+Shift+S や Print Screen は、この画面を撮れないことがある。
    // 枠なしで画面を覆っている間、描いた絵はデスクトップの合成を通らずに
    // そのまま表示へ回されるためで、撮っても1つ前の絵が写る。
    //
    // なので自前で撮る。描き終わった裏画面をそのまま読み戻してpngにする。
    // 出るものと寸分違わない絵が残るので、報告書やポートフォリオにも使える

    // 描き終わった裏画面を、読み戻せる置き場へ写す命令を積む。
    // 積むだけで、実際に読めるようになるのはGPUを待った後（SaveScreenshotFile）
    void CopyBackBufferForScreenshot(UINT backBufferIndex) {
        ID3D12Resource* backBuffer = gfx_.swapChainResources[backBufferIndex].Get();
        D3D12_RESOURCE_DESC desc = backBuffer->GetDesc();

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 totalBytes = 0;
        gfx_.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);

        // 読み戻し用の置き場は撮るたびに作り直す。撮るのは人が押したときだけなので、
        // 持ち回して数MBを抱え続けるより、その場で作って捨てる方が素直
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = gfx_.device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&screenshotReadback_));
        if (FAILED(hr)) {
            Log("[Screenshot] 読み戻す場所を作れませんでした");
            screenshotReadback_.Reset();
            return;
        }
        screenshotFootprint_ = footprint;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        gfx_.commandList->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = backBuffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = screenshotReadback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        gfx_.commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        gfx_.commandList->ResourceBarrier(1, &barrier);
    }

    // GPUを待った後に呼ぶ。読み戻した中身をpngにして書き出す
    void SaveScreenshotFile() {
        if (!screenshotReadback_) { return; }

        uint8_t* mapped = nullptr;
        HRESULT hr = screenshotReadback_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            Log("[Screenshot] 読み戻せませんでした");
            screenshotReadback_.Reset();
            return;
        }

        const UINT width = screenshotFootprint_.Footprint.Width;
        const UINT height = screenshotFootprint_.Footprint.Height;
        const UINT rowPitch = screenshotFootprint_.Footprint.RowPitch;

        // 行の間隔は256の倍数に切り上げられているので、詰め直しながら写す。
        // ついでに不透明にしておく（画面には出ていない不透明度が、pngでは透けて見えてしまう）
        std::vector<uint8_t> pixels((size_t)width * height * 4);
        for (UINT y = 0; y < height; ++y) {
            const uint8_t* sourceRow = mapped + (size_t)y * rowPitch;
            uint8_t* destinationRow = pixels.data() + (size_t)y * width * 4;
            memcpy(destinationRow, sourceRow, (size_t)width * 4);
            for (UINT x = 0; x < width; ++x) { destinationRow[x * 4 + 3] = 255; }
        }
        screenshotReadback_->Unmap(0, nullptr);
        screenshotReadback_.Reset();

        std::error_code errorCode;
        std::filesystem::create_directories(kScreenshotDirectory, errorCode);

        // 名前は撮った時刻。同じ秒に2枚撮ることは押し方の上ありえない
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &now);
        char name[64];
        std::strftime(name, sizeof(name), "%Y%m%d_%H%M%S.png", &local);
        std::string path = std::string(kScreenshotDirectory) + "/" + name;

        DirectX::Image image{};
        image.width = width;
        image.height = height;
        image.format = screenshotFootprint_.Footprint.Format;
        image.rowPitch = (size_t)width * 4;
        image.slicePitch = pixels.size();
        image.pixels = pixels.data();

        hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
                                    DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
                                    ConvertString(path).c_str());
        if (SUCCEEDED(hr)) {
            Log("[Screenshot] 撮りました: \"" + path + "\"");
        } else {
            Log(std::format("[Screenshot] 書き出しに失敗しました hr=0x{:08X}", (uint32_t)hr));
        }
    }

    // ===== 影 =====
    // 平行光源が進む向きへレイを飛ばして「どこに落ちるか」を調べ、その地点に楕円の染みを置く。
    // 横から見た画面では、影が無いと高さも着地点も読めず、狙って落とす遊びが成立しない。
    //
    // ライトの向きに従うのが要点。真下に固定してしまうと、Directional Lightを傾けたときに
    // 物の陰影（明るい面/暗い面）と影の位置が食い違い、どちらが正しいのか分からない絵になる。
    // 斜めから照らせば影は反対側へ伸び、その伸び具合まで含めて光源の向きと一致する
    void DrawShadows(const Matrix4x4& cameraVP) {
        if (!features_.shadows) { return; }

        // 光の進む向き（正規化）。全成分0にもできるので、長さが無い場合は諦める
        Vector3 lightDir = light_.data->direction;
        float lightLength = sqrtf(lightDir.x * lightDir.x + lightDir.y * lightDir.y + lightDir.z * lightDir.z);
        if (lightLength < 1e-4f) { return; }
        lightDir = { lightDir.x / lightLength, lightDir.y / lightLength, lightDir.z / lightLength };

        // 光がほぼ水平（または下から）だと、地面に落ちる影が無限に伸びて絵として破綻する。
        // 真上寄りから照らしているときだけ影を出す
        if (lightDir.y > -0.15f) { return; }

        gfx_.commandList->SetGraphicsRootSignature(shadowPipeline_.rootSignature.Get());
        gfx_.commandList->SetPipelineState(shadowPipeline_.pso.Get());
        gfx_.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gfx_.commandList->IASetVertexBuffers(0, 1, &shadowQuadMesh_.vbv);
        gfx_.commandList->IASetIndexBuffer(&shadowQuadMesh_.ibv);

        int slot = 0;
        for (const Object3D& caster : objects_) {
            if (slot >= kMaxShadows) { break; }
            // 動かない物・2Dのスプライト・当たり判定を持たない物は影を落とさない
            if (caster.isSprite || caster.isStatic || !caster.usePhysics || !caster.useBoxCollider || caster.isTrigger) { continue; }
            if (!caster.visible) { continue; } // 割れて消えた本体の影が残らないように

            OBB casterOBB = caster.GetWorldOBB();

            // 物の底面の中心から、光の進む向きへレイを飛ばす。
            // 重心から飛ばすと、光が斜めのとき物の内側から出発することになり、
            // 自分の体の厚みぶんだけ影がずれてしまう
            Vector3 origin = casterOBB.center;
            origin.y -= VerticalExtent(casterOBB);

            float distance = 0.0f;
            Vector3 groundPoint{};
            if (!RaycastFrom(caster, origin, lightDir, distance, groundPoint)) { continue; } // 光の先に何も無ければ影も無い
            if (distance > shadowMaxHeight_) { continue; }

            // 高いほど薄く、そして少しだけ大きく。実際の影と同じ振る舞いで、
            // 「今どのくらい浮いているか」が影の濃さと大きさから読み取れるようになる
            float heightRatio = distance / shadowMaxHeight_;
            float alpha = shadowOpacity_ * (1.0f - heightRatio) * (1.0f - heightRatio);
            if (alpha < 0.01f) { continue; }

            // 板は1辺1なので、スケールにはそのまま「影の直径」を渡す（半径ではないことに注意）。
            // 物の幅の1.8倍を基準にしてある。ふちがぼけて薄くなるぶん、
            // 実際に濃く見える部分は物と同じくらいの大きさに落ち着く
            float diameter = HorizontalExtent(casterOBB) * 2.0f * 1.8f * (1.0f + heightRatio * 0.6f);

            // 斜めから照らされた丸い影は、地面の上では楕円に伸びる。
            // 伸びる量は光の傾きで決まり、真上からなら1倍、寝かせるほど長くなる（1/|進む向きのY成分|）。
            // 伸ばす向きは、光が水平方向に流れていく向きに合わせる
            float stretch = (std::min)(3.0f, 1.0f / fabsf(lightDir.y)); // 伸びすぎて不自然にならない範囲で頭打ち
            float yaw = atan2f(lightDir.x, lightDir.z); // 水平成分の向き（この角度だけY軸まわりに回す）

            // 地面のすぐ上に置く。ぴったり同じ高さだと、どちらが手前か決まらずちらつく（Zファイティング）
            Vector3 position{ groundPoint.x, groundPoint.y + kShadowLift, groundPoint.z };
            Matrix4x4 world = MakeAffineMatrix({ diameter, 1.0f, diameter * stretch },
                                               { 0.0f, yaw, 0.0f }, position);

            LineTransformBuffer& cb = shadowTransformBuffers_[slot];
            *cb.data = Multiply(world, cameraVP);

            const float color[4] = { 0.0f, 0.0f, 0.0f, alpha };
            gfx_.commandList->SetGraphicsRootConstantBufferView(0, cb.resource->GetGPUVirtualAddress());
            gfx_.commandList->SetGraphicsRoot32BitConstants(1, 4, color, 0);
            gfx_.commandList->DrawIndexedInstanced(shadowQuadMesh_.indexCount, 1, 0, 0, 0);
            slot++;
        }

        // 通常の描画（Object3DPipeline）に戻しておく
        RestoreObjectPipeline();
    }

    // originから指定方向へレイを飛ばし、一番手前で当たった点とその距離を返す
    bool RaycastFrom(const Object3D& caster, const Vector3& origin, const Vector3& direction,
                     float& outDistance, Vector3& outPoint) {
        float best = (std::numeric_limits<float>::max)();
        bool hit = false;

        auto TestOne = [&](const Object3D& target) {
            if (&target == &caster) { return; }               // 自分自身には落とさない
            if (!target.useBoxCollider || target.isSprite || target.isTrigger) { return; }
            float distance = 0.0f;
            if (RayIntersectOBB(origin, direction, target.GetWorldOBB(), distance) && distance < best) {
                best = distance;
                hit = true;
            }
        };

        for (const Object3D& obj : objects_) { TestOne(obj); }

        if (!hit) { return false; }
        outDistance = best;
        outPoint = { origin.x + direction.x * best, origin.y + direction.y * best, origin.z + direction.z * best };
        return true;
    }

    // 回転したOBBが、ワールドのY軸方向にどれだけ広がっているか（＝底面までの距離）
    static float VerticalExtent(const OBB& obb) {
        return fabsf(obb.axis[0].y) * obb.halfSize.x + fabsf(obb.axis[1].y) * obb.halfSize.y + fabsf(obb.axis[2].y) * obb.halfSize.z;
    }

    // 水平方向の広がり（中心から端まで）の目安。影の大きさを決める基準にする。XとZの平均を取る
    static float HorizontalExtent(const OBB& obb) {
        float x = fabsf(obb.axis[0].x) * obb.halfSize.x + fabsf(obb.axis[1].x) * obb.halfSize.y + fabsf(obb.axis[2].x) * obb.halfSize.z;
        float z = fabsf(obb.axis[0].z) * obb.halfSize.x + fabsf(obb.axis[1].z) * obb.halfSize.y + fabsf(obb.axis[2].z) * obb.halfSize.z;
        return (x + z) * 0.5f;
    }

    static constexpr float kShadowLift = 0.02f; // 地面から影を浮かせる量（ちらつき防止）

    // ===== 2Dの描画の中身 =====
    //
    // 公開している Rect2D などは、その場では描かずに命令を積むだけ。
    // ゲームの処理はフレームの前半で走るが、GPUへ描き込めるのは
    // 描画パスを開いている間だけなので、そのまま描こうとすると
    // 「まだパスが開いていない」か「もう閉じた後」になる。
    //
    // 積んでおいて後でまとめて流す形にすると、呼んだ順がそのまま重なり順になる。
    // ImGuiの描画リストと同じ考え方で、移植のときもこの性質がそろっている。
    //
    // 図形も絵も同じパイプラインで描く。分けると、図形と絵を交互に積んだときに
    // 重なり順が保てない。図形のときは白1x1のテクスチャを貼るので、
    // 指定した色がそのまま出る


    // パスから絵を引く。無ければ読み込んで覚える。
    // 同じ絵を毎フレーム名前で指定しても、読み込みは最初の1回だけになる
    const Loaded2DTexture* GetOr2DLoadTexture(const std::string& path) {
        auto found = textures2D_.find(path);
        if (found != textures2D_.end()) {
            return found->second.srv.ptr != 0 ? &found->second : nullptr;
        }

        Loaded2DTexture loaded;
        // ヒープの外へ書き込むと描画全体が壊れるので、上限で止める
        if (gfx_.nextSrvIndex >= GraphicsContext::kSrvDescriptorCount) {
            Log(std::format("[Sprite] 絵の置き場所が上限({}枚)に達しました: \"{}\"",
                            GraphicsContext::kSrvDescriptorCount, path));
            textures2D_[path] = loaded; // 空のまま覚えて、毎フレーム読みに行かないようにする
            return nullptr;
        }

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            Log(std::format("[Sprite] \"{}\" が見つかりません", path));
            textures2D_[path] = loaded;
            return nullptr;
        }

        // 大きさを知るために、先に中身を読んでから転送する。
        // LoadTextureToSRVは大きさを返さないので、ここでは二段に分けている
        DirectX::ScratchImage image = LoadTexture(path);
        const DirectX::TexMetadata& metadata = image.GetMetadata();
        loaded.width = (float)metadata.width;
        loaded.height = (float)metadata.height;
        loaded.srv = CreateTextureSRV(gfx_.device.Get(), gfx_.commandList.Get(),
                                      gfx_.srvDescriptorHeap.Get(), gfx_.descriptorSizeSRV,
                                      gfx_.nextSrvIndex, std::move(image),
                                      loaded.resource, gfx_.pendingIntermediateResources);
        ++gfx_.nextSrvIndex;
        Log(std::format("[Sprite] \"{}\" を読み込みました（{:.0f}x{:.0f}）",
                        path, loaded.width, loaded.height));

        textures2D_[path] = loaded;
        return &textures2D_[path];
    }

    void Push2D(Draw2DCommand::Kind kind, const Vector2& position, float width, float height,
                float rotation, const Vector4& color, bool throughCamera) {
        if (color.w <= 0.001f) { return; }
        // 積みすぎを止める。上限を超えたぶんを捨てるのは、
        // 何も描かなくなるより「一部が出ない」方が原因を追いやすいため
        if (draw2DCommands_.size() >= kMaxDraw2DCommands) { return; }
        Draw2DCommand command{ kind, position, width, height, rotation, color, throughCamera };
        draw2DCommands_.push_back(command);
    }

    void PushSprite2D(const Loaded2DTexture* texture, const Vector2& position,
                      float width, float height, float rotation, const Vector4& color,
                      bool throughCamera) {
        if (color.w <= 0.001f || !texture) { return; }
        if (draw2DCommands_.size() >= kMaxDraw2DCommands) { return; }
        Draw2DCommand command{ Draw2DCommand::Kind::Quad, position, width, height,
                               rotation, color, throughCamera };
        command.texture = texture->srv;
        draw2DCommands_.push_back(command);
    }

    // 積んだ2Dの命令を、2D用のパイプラインへ流す
    void FlushDraw2D() {
        if (draw2DCommands_.empty()) { return; }

        gfx_.commandList->SetGraphicsRootSignature(draw2DPipeline_.rootSignature.Get());
        gfx_.commandList->SetPipelineState(draw2DPipeline_.pso.Get());
        gfx_.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        Canvas2D::Fit fit = canvas2D_.MakeFit((float)gfx_.clientWidth, (float)gfx_.clientHeight);
        float aspect = (gfx_.clientHeight > 0)
            ? ((float)gfx_.clientWidth / (float)gfx_.clientHeight) : 1.0f;

        for (const Draw2DCommand& command : draw2DCommands_) {
            if (draw2DDrawCount_ >= (int)draw2DTransformBuffers_.size()) { break; }

            Vector2 canvasPoint = command.throughCamera
                ? WorldToCanvas(camera2D_, canvas2D_, command.position)
                : command.position;
            float zoom = command.throughCamera ? camera2D_.zoom : 1.0f;

            Vector2 ndcCenter = CanvasToNdc(canvas2D_, fit, canvasPoint);
            Vector2 ndcSize = CanvasSizeToNdc(fit, command.width * zoom, command.height * zoom);

            LineTransformBuffer& cb = draw2DTransformBuffers_[draw2DDrawCount_++];
            if (fabsf(command.rotation) < 0.0001f) {
                *cb.data = MakeScreenRectMatrix(ndcCenter.x, ndcCenter.y, ndcSize.x, ndcSize.y);
            } else {
                // 傾けるときは縦の目盛りにそろえてから回す（横長の画面でひしゃげないように）。
                // 論理座標は下がプラス、NDCは上がプラスなので、回る向きは逆になる
                *cb.data = MakeScreenRectMatrixRotated(ndcCenter.x, ndcCenter.y, ndcSize.x, ndcSize.y,
                                                       -command.rotation, aspect);
            }

            const Draw2DMesh& mesh = (command.kind == Draw2DCommand::Kind::Circle)   ? draw2DCircleMesh_
                                    : (command.kind == Draw2DCommand::Kind::Triangle) ? draw2DTriangleMesh_
                                                                                      : draw2DQuadMesh_;
            // 絵が指定されていなければ白1x1。図形はこれで、指定した色がそのまま出る
            D3D12_GPU_DESCRIPTOR_HANDLE srv = (command.texture.ptr != 0)
                ? command.texture : textures_.whiteSrv;

            const float rgba[4] = { command.color.x, command.color.y, command.color.z, command.color.w };
            gfx_.commandList->SetGraphicsRootConstantBufferView(0, cb.resource->GetGPUVirtualAddress());
            gfx_.commandList->SetGraphicsRoot32BitConstants(1, 4, rgba, 0);
            gfx_.commandList->SetGraphicsRootDescriptorTable(2, srv);
            gfx_.commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
            gfx_.commandList->IASetIndexBuffer(&mesh.ibv);
            gfx_.commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        }

        draw2DCommands_.clear();
    }

    // 通常の描画（Object3DPipeline）へ戻す。
    //
    // 平行光源の指定をここで入れ直すのが要点。ルートシグネチャを切り替えると、
    // それまでに渡した引数はすべて無効になる。以前は「オブジェクトを描き終えてから
    // 別のパイプラインへ移る」順番しか無かったので気づかなかったが、
    // 幕を先に張ってから3Dを描く場面ができると、光の情報が抜けたまま描いて真っ黒になる
    void RestoreObjectPipeline() {
        gfx_.commandList->SetGraphicsRootSignature(pipeline_.rootSignature.Get());
        gfx_.commandList->SetPipelineState(pipeline_.pso.Get());
        gfx_.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gfx_.commandList->SetGraphicsRootConstantBufferView(3, light_.resource->GetGPUVirtualAddress());
    }

    // 各オブジェクトのコライダーをワイヤーフレームで描画する（Unityのギズモ風）
    //   ・ボックスコライダー（OBB）：useBoxCollider が true のオブジェクトのみ、ユニットボックスを変形して描画
    //   ・メッシュコライダー：useMeshCollider が true のオブジェクトのみ、実際の三角形の辺に沿って描画
    // 衝突していれば赤、していなければ緑（メッシュコライダーはUpdatePhysicsで求めた実際の衝突結果を使う）
    void DrawColliders(const Matrix4x4& cameraVP, const Matrix4x4& spriteVP) {
        gfx_.commandList->SetGraphicsRootSignature(linePipeline_.rootSignature.Get());
        gfx_.commandList->SetPipelineState(linePipeline_.pso.Get());
        gfx_.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

        const float kGreen[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        const float kRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

        // 各オブジェクトのワールドOBBをあらかじめ求め、ボックスコライダー同士で総当たりの衝突判定をしておく
        // （useBoxColliderがOFFのオブジェクトは、押す/押される判定と同じく表示上の衝突判定からも除外する）
        std::vector<OBB> worldOBBs;
        worldOBBs.reserve(objects_.size());
        for (auto& obj : objects_) { worldOBBs.push_back(obj.GetWorldOBB()); }

        std::vector<bool> isColliding(objects_.size(), false);
        for (size_t i = 0; i < worldOBBs.size(); ++i) {
            if (!objects_[i].useBoxCollider) { continue; }
            for (size_t j = i + 1; j < worldOBBs.size(); ++j) {
                if (!objects_[j].useBoxCollider) { continue; }
                if (Intersect(worldOBBs[i], worldOBBs[j])) {
                    isColliding[i] = true;
                    isColliding[j] = true;
                }
            }
        }

        int slot = 0;

        // ----- ボックスコライダーの描画（useBoxCollider が true のオブジェクトのみ） -----
        gfx_.commandList->IASetVertexBuffers(0, 1, &lineBoxMesh_.vbv);
        gfx_.commandList->IASetIndexBuffer(&lineBoxMesh_.ibv);
        for (size_t i = 0; i < objects_.size(); ++i) {
            if (slot >= kMaxColliderBoxes) { break; } // 確保しておいたCBVプールを使い切ったら以降は諦める
            Object3D& obj = objects_[i];
            if (!obj.useBoxCollider) { continue; } // OFFのオブジェクトはボックスを表示しない

            const OBB& worldOBB = worldOBBs[i];
            // ユニットボックス（0〜1）をOBBの向き・大きさに変形し、最小コーナーへ平行移動するだけでOBBそのものになる
            Matrix4x4 boxWorldMatrix = MakeOBBWorldMatrix(worldOBB);
            const Matrix4x4& vp = obj.isSprite ? spriteVP : cameraVP;

            LineTransformBuffer& cb = lineTransformBuffers_[slot];
            *cb.data = Multiply(boxWorldMatrix, vp);

            gfx_.commandList->SetGraphicsRootConstantBufferView(0, cb.resource->GetGPUVirtualAddress());
            gfx_.commandList->SetGraphicsRoot32BitConstants(1, 4, isColliding[i] ? kRed : kGreen, 0);
            gfx_.commandList->DrawIndexedInstanced(lineBoxMesh_.indexCount, 1, 0, 0, 0);
            slot++;
        }

        // ----- メッシュコライダーの描画（useMeshCollider が true のオブジェクトのみ、実メッシュの辺に沿って表示） -----
        for (size_t i = 0; i < objects_.size(); ++i) {
            if (slot >= kMaxColliderBoxes) { break; }
            Object3D& obj = objects_[i];
            if (!obj.useMeshCollider) { continue; }

            // ワイヤーフレーム用の頂点/インデックスバッファは初回描画時に1度だけ作り、以後は使い回す
            if (!obj.meshLineData.built) {
                obj.meshLineData = CreateMeshWireframeLineData(gfx_.device.Get(), obj.localPositions, obj.triangleIndices);
                if (!obj.meshLineData.built) { continue; } // メッシュ情報が無ければ描画しない
            }

            gfx_.commandList->IASetVertexBuffers(0, 1, &obj.meshLineData.vbv);
            gfx_.commandList->IASetIndexBuffer(&obj.meshLineData.ibv);

            Matrix4x4 worldMatrix = MakeAffineMatrix(obj.transform.scale, obj.transform.rotate, obj.transform.translate);
            const Matrix4x4& vp = obj.isSprite ? spriteVP : cameraVP;

            LineTransformBuffer& cb = lineTransformBuffers_[slot];
            *cb.data = Multiply(worldMatrix, vp);

            const std::vector<bool>& meshFlags = physics_.MeshCollidingFlags();
            bool colliding = (i < meshFlags.size()) && meshFlags[i];
            gfx_.commandList->SetGraphicsRootConstantBufferView(0, cb.resource->GetGPUVirtualAddress());
            gfx_.commandList->SetGraphicsRoot32BitConstants(1, 4, colliding ? kRed : kGreen, 0);
            gfx_.commandList->DrawIndexedInstanced(obj.meshLineData.indexCount, 1, 0, 0, 0);
            slot++;
        }

        // 通常の描画（Object3DPipeline）に戻しておく。次に描くものが別のPSO前提で命令を積むため
        RestoreObjectPipeline();
    }

    // ログディレクトリ・ログファイルの初期化
    static void InitializeLogFile() {
        std::filesystem::create_directory("logs");
        logStream.open("logs/latest.log");
    }
};
