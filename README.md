# Aogine

DirectX12 の自作エンジンの上で、チームでゲームを作るためのプロジェクト。
エディタは無く、**すべてコードで書く**。

## はじめかた

1. clone して `Aogine.slnx` を Visual Studio で開く（x64 のみ）
2. F5 で起動。床が映れば準備完了（F2 でデバッグビューに切り替わる）

ソリューションに並ぶのは Aogine だけ。画像読み込みの DirectXTex は、Aogine をビルドするときに自動で一緒に組み立てられる
（初回だけ少し時間がかかる）。

ビルドには **Visual Studio 2026**（プラットフォームツールセット v145）が要る。DirectXTex も同じ v145 で組まれる。

## フォルダ

| 場所 | 中身 | 触るか |
|---|---|---|
| `Game/` | ゲームのコード。入口は `Game/Game.cpp` | **ここに書く** |
| `resources/` | モデル・画像・音 | 素材はここへ置く |
| `Engine/` | エンジン本体 | 原則触らない（直すときはチームに一声かける） |
| `*.hlsl` | シェーダ（起動時にコンパイルされる） | 触らない |
| `externals/` | DirectXTex・DXC | 触らない |

## 書き方の最小形

```cpp
void SetupScene(Engine& engine) {
    Object3D& box = engine.AddCube();
    uint32_t boxId = box.id;          // 参照は使い回さず、idで控える

    engine.SetUpdateCallback([&engine, boxId](float dt) {
        if (Object3D* b = engine.FindById(boxId)) {
            if (engine.Input().IsKeyDown(DIK_D)) { b->transform.translate.x += 3.0f * dt; }
        }
    });
}
```

使える関数の一覧は **[開発README.md](開発README.md)** にある。

## デバッグビュー（F2）

Debug ビルドで起動中に **F2** を押すと、ゲームを止めて Unity の Scene ビューのような画面になる。
クリックで物を選び、W/E/R で 移動/回転/拡縮 のギズモを切り替えてドラッグで動かせる。
右ドラッグ＋WASD で視点移動、F で選んだ物へ寄る。もう一度 F2 でゲームへ戻る。
詳しくは 開発README.md の「13. デバッグビュー」。

## チームでの決まり

- `.cpp` を足したら、ソリューション エクスプローラーの `Game` フォルダへ「追加」する（`Aogine.vcxproj` に登録される）
- ソースは **BOM無しの UTF-8** で保存する（日本語の文字列が化けないように）
- 警告はエラー扱い（`/WX`）。警告ゼロでコミットする
- `x64/`・`.vs/`・`logs/`・`screenshots/` はコミットしない（`.gitignore` 済み）

## 提出用（Release）

Release でビルドし、`x64/Release/Aogine.exe` と `dxcompiler.dll`・`dxil.dll`、
リポジトリ直下の `*.hlsl`・`*.hlsli`、`resources/` を同じフォルダに並べれば動く。
