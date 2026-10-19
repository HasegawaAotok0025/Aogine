#pragma once
#include <cmath>
#include <utility>

// ===== 数学・グラフィックス用構造体定義 =====
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Vector4 { float x, y, z, w; };
struct Matrix4x4 { float m[4][4]; };
struct Transform { Vector3 scale; Vector3 rotate; Vector3 translate; };

// ===== 円周率と角度の変換 =====
inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = kPi * 2.0f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

// ===== Vector3の計算 =====
// 物理・当たり判定・ギズモ・カメラのどこでも使うので、ここに1つだけ置く
inline Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 Scale(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
// p + dir * s（ある点から、ある向きへ s だけ進んだ点）
inline Vector3 AddScaled(const Vector3& p, const Vector3& dir, float s) {
    return { p.x + dir.x * s, p.y + dir.y * s, p.z + dir.z * s };
}
inline float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector3 Cross(const Vector3& a, const Vector3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float LengthSq(const Vector3& v) { return Dot(v, v); }
inline float Length(const Vector3& v) { return std::sqrt(Dot(v, v)); }
// 長さ1にする。長さがほぼ0で向きが決まらないときは fallback を返す
inline Vector3 Normalize(const Vector3& v, const Vector3& fallback = { 0.0f, 1.0f, 0.0f }) {
    float lenSq = LengthSq(v);
    if (lenSq < 1e-12f) { return fallback; }
    return Scale(v, 1.0f / std::sqrt(lenSq));
}

// 単位行列の作成
inline Matrix4x4 MakeIdentity4x4() {
    Matrix4x4 mat{};
    mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = mat.m[3][3] = 1.0f;
    return mat;
}

// 行列の乗算
inline Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 result{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                result.m[i][j] += a.m[i][k] * b.m[k][j];
    return result;
}

// アフィン変換行列の作成（SRT合成）
inline Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
    Matrix4x4 rx{};
    rx.m[0][0] = 1;
    rx.m[1][1] = cosf(rotate.x); rx.m[1][2] = sinf(rotate.x);
    rx.m[2][1] = -sinf(rotate.x); rx.m[2][2] = cosf(rotate.x);
    rx.m[3][3] = 1;

    Matrix4x4 ry{};
    ry.m[0][0] = cosf(rotate.y); ry.m[0][2] = -sinf(rotate.y);
    ry.m[1][1] = 1;
    ry.m[2][0] = sinf(rotate.y); ry.m[2][2] = cosf(rotate.y);
    ry.m[3][3] = 1;

    Matrix4x4 rz{};
    rz.m[0][0] = cosf(rotate.z); rz.m[0][1] = sinf(rotate.z);
    rz.m[1][0] = -sinf(rotate.z); rz.m[1][1] = cosf(rotate.z);
    rz.m[2][2] = 1;
    rz.m[3][3] = 1;

    Matrix4x4 rot = Multiply(rx, Multiply(ry, rz));
    Matrix4x4 result{};
    result.m[0][0] = scale.x * rot.m[0][0]; result.m[0][1] = scale.x * rot.m[0][1]; result.m[0][2] = scale.x * rot.m[0][2];
    result.m[1][0] = scale.y * rot.m[1][0]; result.m[1][1] = scale.y * rot.m[1][1]; result.m[1][2] = scale.y * rot.m[1][2];
    result.m[2][0] = scale.z * rot.m[2][0]; result.m[2][1] = scale.z * rot.m[2][1]; result.m[2][2] = scale.z * rot.m[2][2];
    result.m[3][0] = translate.x; result.m[3][1] = translate.y; result.m[3][2] = translate.z; result.m[3][3] = 1.0f;
    return result;
}

// 平行投影（オーソグラフィック）行列の作成 - 2Dスプライト用
inline Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
    Matrix4x4 mat{};
    mat.m[0][0] = 2.0f / (right - left);
    mat.m[1][1] = 2.0f / (top - bottom);
    mat.m[2][2] = 1.0f / (farClip - nearClip);
    mat.m[3][0] = (left + right) / (left - right);
    mat.m[3][1] = (top + bottom) / (bottom - top);
    mat.m[3][2] = nearClip / (nearClip - farClip);
    mat.m[3][3] = 1.0f;
    return mat;
}

// 透視投影（パースペクティブ）行列の作成 - 3D用
inline Matrix4x4 MakePerspectiveMatrix(float fovY, float aspect, float nearZ, float farZ) {
    Matrix4x4 mat{};
    mat.m[0][0] = 1.0f / (aspect * tanf(fovY * 0.5f));
    mat.m[1][1] = 1.0f / tanf(fovY * 0.5f);
    mat.m[2][2] = farZ / (farZ - nearZ);
    mat.m[2][3] = 1.0f;
    mat.m[3][2] = -nearZ * farZ / (farZ - nearZ);
    return mat;
}

// 4x4行列の逆行列を掃き出し法（ガウス・ジョルダン法）で求める
// CameraのView行列計算（WorldMatrixの逆行列）などに利用する
inline Matrix4x4 Inverse4x4(const Matrix4x4& src) {
    // 拡大行列 [src | I] を作り、掃き出し法でsrcを単位行列に変形しながら
    // 右側(4〜7列目)に逆行列を作っていく
    float a[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            a[i][j] = src.m[i][j];
            a[i][j + 4] = (i == j) ? 1.0f : 0.0f;
        }
    }

    for (int pivot = 0; pivot < 4; pivot++) {
        // ピボット選択（数値安定のため絶対値最大の行を探す）
        int maxRow = pivot;
        float maxVal = fabsf(a[pivot][pivot]);
        for (int row = pivot + 1; row < 4; row++) {
            if (fabsf(a[row][pivot]) > maxVal) {
                maxVal = fabsf(a[row][pivot]);
                maxRow = row;
            }
        }
        if (maxRow != pivot) {
            for (int col = 0; col < 8; col++) { std::swap(a[pivot][col], a[maxRow][col]); }
        }

        float pivotVal = a[pivot][pivot];
        if (fabsf(pivotVal) < 1e-8f) {
            // 逆行列が存在しない場合は単位行列を返しておく（安全側の挙動）
            return MakeIdentity4x4();
        }
        for (int col = 0; col < 8; col++) { a[pivot][col] /= pivotVal; }

        for (int row = 0; row < 4; row++) {
            if (row == pivot) { continue; }
            float factor = a[row][pivot];
            for (int col = 0; col < 8; col++) { a[row][col] -= factor * a[pivot][col]; }
        }
    }

    Matrix4x4 result{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = a[i][j + 4];
        }
    }
    return result;
}

// 点をワールド行列で変換する（このプロジェクト共通の「行ベクトル×行列」規約: p' = p * M）
// BoxCollider.cpp/MeshCollider.cppにも同等のローカル関数があるが、それらに加えて
// Engine.h側（メッシュ頂点のワールド変換など）からも使えるよう、ここに公開版を置いておく
inline Vector3 TransformPoint(const Vector3& p, const Matrix4x4& m) {
    return {
        p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
        p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
        p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2],
    };
}

// ワールド座標が画面のどこに映るかを求める（ピクセル単位。左上が0,0）。
// カメラの後ろにある点は画面に出しようがないのでfalseを返す。
//
// 判定を省いてそのまま割ると、後ろにある物が符号の反転した位置に「もう1つ」現れる。
// 振り返った瞬間に、見えないはずの数字が画面の反対側に出るという妙な出方をする
inline bool WorldToScreen(const Vector3& world, const Matrix4x4& viewProjection,
                          float screenWidth, float screenHeight, Vector2& out) {
    const Matrix4x4& m = viewProjection;
    float x = world.x * m.m[0][0] + world.y * m.m[1][0] + world.z * m.m[2][0] + m.m[3][0];
    float y = world.x * m.m[0][1] + world.y * m.m[1][1] + world.z * m.m[2][1] + m.m[3][1];
    float w = world.x * m.m[0][3] + world.y * m.m[1][3] + world.z * m.m[2][3] + m.m[3][3];
    if (w <= 1e-5f) { return false; }

    out.x = (x / w * 0.5f + 0.5f) * screenWidth;
    out.y = (0.5f - y / w * 0.5f) * screenHeight; // 画面のYは下向きなので上下を入れ替える
    return true;
}
