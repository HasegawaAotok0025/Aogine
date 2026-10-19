#pragma once
#include "MathUtility.h"

// ============================================================================
// クォータニオン（四元数）：剛体の姿勢を表すための回転表現
//
// なぜオイラー角ではだめなのか：
//   物理シミュレーションでは毎ステップ「角速度ω × 微小時間」だけ姿勢を進める必要がある。
//   ところがオイラー角(x,y,z)は3つの回転を順番に掛けたものなので、
//   rotate.x += ω.x*dt のように各成分へ足しても、それは「ω方向まわりの回転」にはならない。
//   1軸だけ回っているうちは偶然一致するが、2軸以上が同時に回った途端に姿勢が狂う。
//   さらにピッチが±90度になると別の軸が縮退して回転が失われる（ジンバルロック）。
//   クォータニオンにはこの問題が無く、どの向きからでも正しく積分できる。
//
// このエンジンの規約に合わせてあること：
//   ・行ベクトル（v' = v * M）で、MakeAffineMatrixが rot = Rx * Ry * Rz を作る
//   ・したがってベクトルにはX→Y→Zの順に回転が適用される
//   ・EulerXYZFromQuaternion / QuaternionFromEulerXYZ はこの順序と完全に一致させてある
//     （見た目の行列が変わらないので、既存の描画・当たり判定コードに手を入れずに済む）
// ============================================================================

struct Quaternion {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; // 既定は「回転なし」
};

inline Quaternion QuaternionIdentity() { return Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f }; }

// ハミルトン積。rotate(a, rotate(b, v)) == rotate(a*b, v) の関係が成り立つ
inline Quaternion Multiply(const Quaternion& a, const Quaternion& b) {
    return Quaternion{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline Quaternion Normalize(const Quaternion& q) {
    float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lenSq < 1e-12f) { return QuaternionIdentity(); }
    float inv = 1.0f / sqrtf(lenSq);
    return Quaternion{ q.x * inv, q.y * inv, q.z * inv, q.w * inv };
}

// 共役＝逆回転（正規化されている前提）
inline Quaternion Conjugate(const Quaternion& q) { return Quaternion{ -q.x, -q.y, -q.z, q.w }; }

// 軸（正規化済み）まわりにangle[rad]だけ回すクォータニオン
inline Quaternion QuaternionFromAxisAngle(const Vector3& axis, float angle) {
    float half = angle * 0.5f;
    float s = sinf(half);
    return Quaternion{ axis.x * s, axis.y * s, axis.z * s, cosf(half) };
}

// ローカル→ワールドへベクトルを回す。v * R(q) と同じ結果になる
inline Vector3 RotateVector(const Quaternion& q, const Vector3& v) {
    Vector3 u{ q.x, q.y, q.z };
    Vector3 uv{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
    Vector3 uuv{ u.y * uv.z - u.z * uv.y, u.z * uv.x - u.x * uv.z, u.x * uv.y - u.y * uv.x };
    return Vector3{
        v.x + 2.0f * (q.w * uv.x + uuv.x),
        v.y + 2.0f * (q.w * uv.y + uuv.y),
        v.z + 2.0f * (q.w * uv.z + uuv.z),
    };
}

// クォータニオン→回転行列（行ベクトル規約。MakeAffineMatrixの回転部分と同じ形）
inline Matrix4x4 MatrixFromQuaternion(const Quaternion& q) {
    Matrix4x4 m = MakeIdentity4x4();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    m.m[0][0] = 1.0f - 2.0f * (yy + zz); m.m[0][1] = 2.0f * (xy + wz);        m.m[0][2] = 2.0f * (xz - wy);
    m.m[1][0] = 2.0f * (xy - wz);        m.m[1][1] = 1.0f - 2.0f * (xx + zz); m.m[1][2] = 2.0f * (yz + wx);
    m.m[2][0] = 2.0f * (xz + wy);        m.m[2][1] = 2.0f * (yz - wx);        m.m[2][2] = 1.0f - 2.0f * (xx + yy);
    return m;
}

// オイラー角(XYZ)→クォータニオン。
// MakeAffineMatrixが rot = Rx * Ry * Rz を作り、行ベクトルにはX→Y→Zの順で適用されるので、
// 回転の合成は qz * qy * qx の順になる（後から掛かるものが左）
inline Quaternion QuaternionFromEulerXYZ(const Vector3& euler) {
    Quaternion qx = QuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, euler.x);
    Quaternion qy = QuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, euler.y);
    Quaternion qz = QuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, euler.z);
    return Normalize(Multiply(qz, Multiply(qy, qx)));
}

// クォータニオン→オイラー角(XYZ)。上のQuaternionFromEulerXYZの逆変換。
// Rx*Ry*Rzを展開すると m02 = -sin(y), m01/m00 = tan(z), m12/m22 = tan(x) になることから求める。
// これは「表示・編集用にオイラー角へ戻す」ためのもので、姿勢の実体はあくまでクォータニオン側にある
// （Unityのインスペクターと同じ考え方。同じ姿勢でも表示される数値の組み合わせは複数あり得る）
inline Vector3 EulerXYZFromQuaternion(const Quaternion& q) {
    Matrix4x4 m = MatrixFromQuaternion(q);
    Vector3 euler{};

    float sinY = -m.m[0][2];
    sinY = (std::max)(-1.0f, (std::min)(1.0f, sinY));
    euler.y = asinf(sinY);

    if (fabsf(m.m[0][2]) < 0.9999f) {
        euler.z = atan2f(m.m[0][1], m.m[0][0]);
        euler.x = atan2f(m.m[1][2], m.m[2][2]);
    } else {
        // ジンバルロック（cos(y)≒0）。XとZが同じ回転を表してしまうので、Zを0に固定してXへまとめる
        euler.z = 0.0f;
        euler.x = (m.m[0][2] < 0.0f) ? atan2f(m.m[1][0], m.m[1][1]) : atan2f(-m.m[1][0], m.m[1][1]);
    }
    return euler;
}

// 角速度ω（ワールド、rad/s）でdt秒だけ姿勢を進める。
// dq/dt = 0.5 * (ωを純虚四元数にしたもの) * q という剛体運動の基本式そのもの
inline Quaternion IntegrateQuaternion(const Quaternion& q, const Vector3& angularVelocity, float deltaTime) {
    Quaternion omega{ angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f };
    Quaternion derivative = Multiply(omega, q);
    Quaternion result{
        q.x + derivative.x * 0.5f * deltaTime,
        q.y + derivative.y * 0.5f * deltaTime,
        q.z + derivative.z * 0.5f * deltaTime,
        q.w + derivative.w * 0.5f * deltaTime,
    };
    return Normalize(result); // 積分の誤差で長さが1からずれるため、毎回そろえ直す
}
