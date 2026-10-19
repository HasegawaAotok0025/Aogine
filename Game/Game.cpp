#include "../App.h"
#include "../Engine/Engine.h"

// ============================================================================
// ゲームの入口。ここから書き始める。
//
// 今入っているのは、エンジンの使い方を見せるための小さな見本：
//   床・半透明の箱・Instancingで並べた板・火のようなパーティクル
// 作り始めるときは、中身を消して自分のものに置き換えてよい。
//
// 使える関数の一覧は 開発README.md を参照。
// ============================================================================



void SetupScene(Engine& engine) {
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
}

AppStartup GetAppStartup() {
    AppStartup startup; // 既定値は App.h（題 "Aogine"・窓で起動・sRGBへ直す）
    // 変えたいものだけここで上書きする。例：startup.fullscreen = true;
    return startup;
}
