#pragma once
#include "../App.h"
#include <cstdint>
#include <memory>

class Engine;
class Player;

// ============================================================================
// Game：ゲーム全体。main.cppが作り、エンジンの前後で呼ぶ
//
//   GetStartup() … 起動設定（窓の題・全画面か）。エンジンを作る前に1回
//   Initialize() … エンジンを作った直後に1回。物を置き、毎フレームの処理を登録する
//   Update()     … 毎フレーム（エンジンの物理・部品の更新の後、描画の前）
//   Finalize()   … 終わる直前に1回（エンジンを片付ける前）
//
// 今入っているのは、エンジンの使い方を見せるための小さな見本：
//   床・半透明の箱・Instancingで並べた板・火のようなパーティクル・
//   WASDで動く箱・Mキーで形が切り替わるモデル・絵の一部を切り取ったアイコン
// 作り始めるときは、中身を消して自分のものに置き換えてよい。使える関数の一覧は 開発README.md
// ============================================================================
class Game {
public:
    Game();
    ~Game(); // Playerの中身を知っているGame.cppで片付けるため、ここでは宣言だけ

    AppStartup GetStartup() const;
    void Initialize(Engine& engine);
    void Update(float deltaTime);
    void Finalize();

private:
    Engine* engine_ = nullptr;
    std::unique_ptr<Player> player_; // キーで動かす箱
    uint32_t modelId_ = 0;           // Mキーで形を切り替えるモデル
    bool showingSuzanne_ = true;
};
