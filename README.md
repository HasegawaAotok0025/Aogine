# Aogine

DirectX12 の自作エンジン。置く物も動きも **コードで書く**。
Debug ビルドでは、F2 のデバッグビューで置いた物の位置や色を画面で調整し、保存できる。

## はじめかた

1. clone して `Aogine.slnx` を Visual Studio で開く（x64 のみ）
2. F5 で起動。床が映れば準備完了（F2 でデバッグビューに切り替わる）

ソリューションに並ぶのは Aogine だけ。画像読み込みの DirectXTex は、Aogine をビルドするときに自動で一緒に組み立てられる
（初回だけ少し時間がかかる）。

ビルドには **Visual Studio 2022 以降** が要る。ツールセットは開いた Visual Studio の既定のもの（2022 なら v143、2026 なら v145）が使われ、DirectXTex も同じもので組まれる。

## ビルド構成

| 構成 | 中身 |
|---|---|
| Debug | 最適化なし。ImGui とデバッグビュー（F2）入り |
| Development | エンジン層（`Engine/` の .cpp）だけ /O2 で最適化し、アプリ層は最適化なし。`assert` は有効。ImGui なし |
| Release | 全体を最適化。配布用 |

## フォルダ

| 場所 | 中身 | 触るか |
|---|---|---|
| `Game/` | ゲームのコード。入口は `Game/Game.cpp` | **ここに書く** |
| `resources/` | モデル・画像・音 | 素材はここへ置く |
| `Engine/` | エンジン本体 | ゲームを作るときは原則触らない |
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

## 決まり

- `.cpp` を足したら、ソリューション エクスプローラーの `Game` フォルダへ「追加」する（`Aogine.vcxproj` に登録される）
- ソースは **BOM無しの UTF-8** で保存する（日本語の文字列が化けないように）
- 警告はエラー扱い（`/WX`）。警告ゼロでコミットする
- `generated/`（ビルドの出力と中間ファイル）・`.vs/`・`logs/`・`screenshots/`・`saved_edits.txt`・`saved_particles.txt` はコミットしない（`.gitignore` 済み。`saved_*.txt` は F2 で調整した内容で、手元だけに残る）

## 提出用（Release）

Release でビルドし、`generated/outputs/Release/` にできる `Aogine.exe`・`dxcompiler.dll`・`dxil.dll` と、
リポジトリ直下の `*.hlsl`・`*.hlsli`、`resources/` を同じフォルダに並べれば動く。
