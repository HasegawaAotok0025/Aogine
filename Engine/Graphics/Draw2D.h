#pragma once
#include "../Core/MathUtility.h"
#include <d3d12.h> // 貼る絵の指し先（D3D12_GPU_DESCRIPTOR_HANDLE）を持つため

// ============================================================================
// 2Dの絵を「画面のピクセル」で描くための座標とカメラ。
//
// ■ 座標の決まり
//
//   左上が原点、右がプラス、下がプラス。回転は時計回り。
//   これは2Dのゲームで一番よくある取り方で、画像編集の道具ともそろっている。
//   3D側（Y+が上）とは上下が逆になるが、2Dを書いている間は3Dのことを
//   考えなくて済む方が大事なので、ここでは2Dの流儀に合わせる。
//
// ■ 論理解像度
//
//   ゲームは「1280×720の画面に描くつもり」で座標を書き、
//   実際の窓がいくら大きくても小さくても同じ絵が出る。
//   窓の大きさに合わせて座標を書くと、解像度が変わるたびに配置が崩れる。
//
//   窓の縦横比が論理解像度と違うときは、はみ出させずに収める（レターボックス）。
//   引き伸ばすと絵が歪み、切り詰めると端に置いたものが見えなくなる。
//
// ■ カメラ
//
//   見ている場所・寄り・揺れを持つ。描く側は世界の座標をそのまま渡し、
//   画面のどこへ出るかはここが決める。
//   HUDのように「カメラの外」に置きたいものは、カメラを通さずに描く。
// ============================================================================

struct Camera2D {
    // 画面の真ん中に映したい場所（世界の座標）
    Vector2 focus{ 640.0f, 360.0f };

    // 寄り。1で等倍、2で2倍に寄る
    float zoom = 1.0f;

    // 揺れ。毎フレーム減っていく。Shake()で足す
    Vector2 shakeOffset{ 0.0f, 0.0f };
    float shakeStrength = 0.0f;
    float shakeDecayPerSecond = 8.0f;

    // 揺らす。今より弱い指示は無視する（弱い揺れが強い揺れを打ち消さないように）
    void Shake(float strength) {
        if (strength > shakeStrength) { shakeStrength = strength; }
    }

    void Update(float deltaTime) {
        if (shakeStrength <= 0.0f) {
            shakeOffset = { 0.0f, 0.0f };
            return;
        }
        // 毎フレーム向きを変えて振る。同じ向きに振り続けると
        // 「揺れ」ではなく「ずれ」に見える
        shakeAngle_ += 37.0f * deltaTime; // 適当な速さ。周期が揃わない値にしてある
        shakeOffset = { cosf(shakeAngle_ * 6.1f) * shakeStrength,
                        sinf(shakeAngle_ * 7.3f) * shakeStrength };
        shakeStrength -= shakeDecayPerSecond * deltaTime;
        if (shakeStrength < 0.0f) { shakeStrength = 0.0f; }
    }

    void Reset() {
        focus = { 640.0f, 360.0f };
        zoom = 1.0f;
        shakeStrength = 0.0f;
        shakeOffset = { 0.0f, 0.0f };
        shakeAngle_ = 0.0f;
    }

private:
    float shakeAngle_ = 0.0f;
};

// 論理解像度と、実際の窓の間の対応。
//
// 縦横比が違うときは、はみ出さない方の倍率に合わせて中央へ置く。
// 引き伸ばすと絵が歪み、切り詰めると端に置いたものが見えなくなるので、
// 上下（または左右）に帯が出るのを受け入れる
struct Canvas2D {
    float width = 1280.0f;
    float height = 720.0f;

    // 論理座標 → NDC(-1..1) へ移すのに使う値。窓の大きさが変わるたびに作り直す
    struct Fit {
        float scale = 1.0f;    // 論理1pxが実際の何pxになるか
        float offsetX = 0.0f;  // 実際の画面での余白（レターボックスの帯）
        float offsetY = 0.0f;
        float screenWidth = 1.0f;
        float screenHeight = 1.0f;
    };

    Fit MakeFit(float screenWidth, float screenHeight) const {
        Fit fit;
        fit.screenWidth = (screenWidth > 1.0f) ? screenWidth : 1.0f;
        fit.screenHeight = (screenHeight > 1.0f) ? screenHeight : 1.0f;
        float scaleX = fit.screenWidth / width;
        float scaleY = fit.screenHeight / height;
        fit.scale = (scaleX < scaleY) ? scaleX : scaleY; // はみ出さない方に合わせる
        fit.offsetX = (fit.screenWidth - width * fit.scale) * 0.5f;
        fit.offsetY = (fit.screenHeight - height * fit.scale) * 0.5f;
        return fit;
    }
};

// 世界の座標 → 論理画面の座標（カメラを通す）
inline Vector2 WorldToCanvas(const Camera2D& camera, const Canvas2D& canvas, const Vector2& world) {
    float zoom = (camera.zoom > 0.001f) ? camera.zoom : 0.001f;
    return {
        (world.x - camera.focus.x) * zoom + canvas.width * 0.5f + camera.shakeOffset.x,
        (world.y - camera.focus.y) * zoom + canvas.height * 0.5f + camera.shakeOffset.y,
    };
}

// 論理画面の座標 → 世界の座標（クリックした場所を世界で知りたいときに使う）
inline Vector2 CanvasToWorld(const Camera2D& camera, const Canvas2D& canvas, const Vector2& canvasPoint) {
    float zoom = (camera.zoom > 0.001f) ? camera.zoom : 0.001f;
    return {
        (canvasPoint.x - canvas.width * 0.5f - camera.shakeOffset.x) / zoom + camera.focus.x,
        (canvasPoint.y - canvas.height * 0.5f - camera.shakeOffset.y) / zoom + camera.focus.y,
    };
}

// 論理画面の座標 → NDC(-1..1)。
// NDCは中央が0で上がプラスなので、上下を反転する（論理座標は下がプラス）
inline Vector2 CanvasToNdc(const Canvas2D& canvas, const Canvas2D::Fit& fit, const Vector2& canvasPoint) {
    float screenX = fit.offsetX + canvasPoint.x * fit.scale;
    float screenY = fit.offsetY + canvasPoint.y * fit.scale;
    return {
        screenX / fit.screenWidth * 2.0f - 1.0f,
        1.0f - screenY / fit.screenHeight * 2.0f,
    };
}

// 論理画面での長さ → NDCでの長さ（横・縦それぞれ）
inline Vector2 CanvasSizeToNdc(const Canvas2D::Fit& fit, float widthInCanvas, float heightInCanvas) {
    return {
        widthInCanvas * fit.scale / fit.screenWidth * 2.0f,
        heightInCanvas * fit.scale / fit.screenHeight * 2.0f,
    };
}

// 2Dの描画1回ぶん。積んでおいて、描画パスの中でまとめて流す。
// 四角も円も同じ形で持てる（円は幅＝高さ＝直径）
struct Draw2DCommand {
    enum class Kind { Quad, Circle, Triangle }; // Triangleは+X向きの三角（矢じり）
    Kind kind = Kind::Quad;
    Vector2 position{ 0.0f, 0.0f };
    float width = 0.0f;
    float height = 0.0f;
    float rotation = 0.0f;
    Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool throughCamera = true;

    // 貼る絵。0なら白1x1（＝色だけの図形になる）。
    // ここに持たせているのは、図形と絵を1つの並びで扱って
    // 呼んだ順＝重なり順にするため
    D3D12_GPU_DESCRIPTOR_HANDLE texture{};
};

// 画面全体を-1〜1で表す座標（NDC）に、そのまま四角を置くための行列。
// カメラを一切通さないので、視点や画角がどうなっていても画面上の同じ場所に出る。
//
// 板のメッシュは-0.5〜0.5なので、行0/行1に幅と高さを、行3に中心を入れれば足りる。
// 奥行きは0（一番手前）にしてあり、深度テストを切ったパイプラインと組みで使う
inline Matrix4x4 MakeScreenRectMatrix(float centerX, float centerY, float width, float height) {
    Matrix4x4 rect{};
    rect.m[0][0] = width;
    rect.m[1][1] = height;
    rect.m[3][0] = centerX;
    rect.m[3][1] = centerY;
    rect.m[3][2] = 0.0f;
    rect.m[3][3] = 1.0f;
    return rect;
}

// 画面座標に、傾けた四角を置く。
//
// NDCは縦横で目盛りの粗さが違う（横長の画面では、横方向の1が縦方向の1より長い）。
// そのまま回すと、傾けたとたんに形がひしゃげる。
// いったん縦の目盛りにそろえた世界で回し、最後に横だけ戻すことで、見た目どおりに傾く。
// widthとheightは「画面の縦を2としたときの大きさ」で渡すこと
inline Matrix4x4 MakeScreenRectMatrixRotated(float centerX, float centerY, float width, float height,
                                             float rotation, float aspect) {
    if (aspect <= 1e-4f) { aspect = 1.0f; }
    float c = cosf(rotation);
    float s = sinf(rotation);

    Matrix4x4 rect{};
    rect.m[0][0] = c * width / aspect;
    rect.m[0][1] = s * width;
    rect.m[1][0] = -s * height / aspect;
    rect.m[1][1] = c * height;
    rect.m[3][0] = centerX;
    rect.m[3][1] = centerY;
    rect.m[3][2] = 0.0f;
    rect.m[3][3] = 1.0f;
    return rect;
}
