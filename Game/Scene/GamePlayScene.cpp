#include "GamePlayScene.h"
#include "TitleScene.h"
#include "SceneManager.h"
#include "../Player.h"
#include "../../Engine/Engine.h"

GamePlayScene::GamePlayScene() = default;
GamePlayScene::~GamePlayScene() = default;

void GamePlayScene::Initialize(Engine& engine) {
    engine_ = &engine;
    showingSuzanne_ = true;

    // --- カメラ ---
    engine.GetCamera().SetPosition({ 0.0f, 6.0f, -12.0f });
    engine.GetCamera().LookAt({ 0.0f, 0.0f, 0.0f });

    // --- 床（動かない箱）---
    {
        Object3D& ground = engine.AddCube();
        ground.name = "Ground";
        ground.tag = "Ground";
        ground.transform.scale = { 8.0f, 0.5f, 8.0f };
        ground.transform.translate = { 0.0f, -0.5f, 0.0f };
        ground.isStatic = true;
        ground.untextured = true;
        ground.materialData->color = { 0.35f, 0.38f, 0.45f, 1.0f };
    }

    // --- 半透明の箱（BlendModeの見本。blendModeをkNormal以外にすると合成方法が変わる）---
    {
        Object3D& glass = engine.AddCube();
        glass.name = "Glass";
        glass.transform.translate = { 0.0f, 1.0f, 0.0f };
        glass.untextured = true;
        glass.blendMode = BlendMode::kNormal;
        glass.materialData->color = { 0.3f, 0.7f, 1.0f, 0.5f };
    }

    // --- Instancingの見本：uvCheckerを貼った板ポリを、位置を0.1ずつずらして10枚、1回の描画命令で出す ---
    {
        ParticleEmitter quads;
        quads.name = "InstancingQuads";
        quads.enabled = false;              // 自動では出さない（下で10枚だけ置く）
        quads.rate = 0.0f;
        quads.maxParticles = 10;
        quads.lifetimeMin = quads.lifetimeMax = 1.0e9f; // 消えない
        quads.speedMin = quads.speedMax = 0.0f;
        quads.gravity = { 0.0f, 0.0f, 0.0f };
        quads.sizeStart = quads.sizeEnd = 1.0f;
        quads.startColor = quads.endColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        quads.blendMode = BlendMode::kNone; // 不透明の板
        quads.soft = false;
        quads.billboard = false;            // 向きを変えず、置いたままの板
        quads.uvChecker = true;
        int id = engine.AddParticleEmitter(quads);
        for (int i = 0; i < 10; ++i) {
            float offset = (float)i * 0.1f;
            engine.EmitParticleAt(id, { -4.0f + offset, 2.0f + offset, offset });
        }
    }

    // --- パーティクルの見本（火のような粒。設定はデバッグビュー(F2)のParticlesパネルから変えられる）---
    {
        ParticleEmitter fire;
        fire.name = "Fire";
        fire.position = { 3.0f, 0.5f, 0.0f };
        engine.AddParticleEmitter(fire);
    }

    // --- 音：読み込みは最初の1回だけ（2回目以降のLoadAllは、読み込み済みの音を飛ばす）。鳴らすのは名前で ---
    // LoadAllはMedia Foundationで読むので、ここをmp3などに変えてもそのまま鳴る
    engine.Sounds().Register("jump", "resources/Audio/jump.wav", 0.6f);
    engine.Sounds().LoadAll(engine.Audio());

    // --- キーで動かす箱（Player）。Spaceで跳ぶと "jump" が鳴る ---
    player_ = std::make_unique<Player>();
    player_->Initialize(engine, { 0.0f, 2.0f, -2.0f });

    // --- モデルの差し替えの見本：Mキーで、スザンヌとUVチェッカーの形を切り替える ---
    // 参照ではなくidで控える（物が増えると参照は古い場所を指すため）
    modelId_ = engine.AddModel("resources/SuzannuObj", "suzannu.obj").id;
    if (Object3D* model = engine.FindById(modelId_)) { model->transform.translate = { -3.0f, 1.0f, 0.0f }; }
}

void GamePlayScene::Update(float deltaTime) {
    Engine& engine = *engine_;
    player_->Update(deltaTime);

    if (engine.Input().IsKeyTriggered(DIK_M)) {
        showingSuzanne_ = !showingSuzanne_;
        if (showingSuzanne_) { engine.SetModel(modelId_, "resources/SuzannuObj", "suzannu.obj"); }
        else { engine.SetModel(modelId_, "resources/UVCheckObj", "UVChecker.obj"); }
    }

    // --- 絵の切り取りの見本：512x512の絵の左上4分の1（256x256）だけを、右上に小さく出す ---
    engine.HudSprite2DRegion("resources/uvChecker.png", 1170.0f, 110.0f, 0.0f, 0.0f, 256.0f, 256.0f,
                             0.0f, Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }, 0.5f);
    engine.DrawText(Engine::kUiMarginX, Engine::kUiMarginY,
                    "[WASD] 移動  [Space] ジャンプ  [M] モデルを切り替える  [Esc] タイトルへ", 20.0f);

    if (engine.Input().IsKeyTriggered(DIK_ESCAPE)) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void GamePlayScene::Finalize() {
    // 置いた物はSceneManagerがエンジンごと片付けるので、ここでは自分で作ったPlayerだけを手放す
    player_.reset();
    engine_ = nullptr;
}
