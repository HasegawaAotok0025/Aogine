#pragma once
#include "../App.h"
#include "Scene/SceneManager.h"

class Engine;

// ============================================================================
// Game：ゲーム全体。main.cppが作り、エンジンの前後で呼ぶ
//
//   GetStartup() … 起動設定（窓の題・全画面か）。エンジンを作る前に1回
//   Initialize() … エンジンを作った直後に1回。最初の場面を予約し、毎フレームの処理を登録する
//   Update()     … 毎フレーム（エンジンの物理・部品の更新の後、描画の前）。今の場面へ流す
//   Finalize()   … 終わる直前に1回（エンジンを片付ける前）
//
// 中身は場面（Game/Scene/）ごとに書く。最初はタイトル（TitleScene）から始まる。
// 場面の足し方は TitleScene / GamePlayScene を見本にする。使える関数の一覧は 開発README.md
// ============================================================================
class Game {
public:
    AppStartup GetStartup() const;
    void Initialize(Engine& engine);
    void Update(float deltaTime);
    void Finalize();

private:
    SceneManager sceneManager_;
};
