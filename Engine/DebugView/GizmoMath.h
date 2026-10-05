#pragma once
#include "../Core/MathUtility.h"
#include <cmath>

// ============================================================================
// デバッグビューのギズモ（掴んで動かす目印）の計算。
// Engineの状態を一切見ない純粋な計算だけを置いてある。
//
// ギズモは画面に投影してから描き、当たり判定も画面の上で行う。
// 描いた形と掴める場所を同じ計算から作るので、「見えている線をそのまま掴める」。
// ============================================================================

namespace Gizmo {

// 3軸と、回転の輪1本ぶんの「輪が乗る平面を張る2方向(U,V)」
inline constexpr Vector3 kAxis[3]  = { {1,0,0}, {0,1,0}, {0,0,1} };
inline constexpr Vector3 kRingU[3] = { {0,1,0}, {0,0,1}, {1,0,0} };
inline constexpr Vector3 kRingV[3] = { {0,0,1}, {1,0,0}, {0,1,0} };

// U×V がその軸そのものになっていることをビルド時に確かめる。
// ずれると「輪をドラッグした向き」と「実際に回る向き」が逆になる
constexpr bool RingBasisMatchesAxis(int a) {
    return kRingU[a].y * kRingV[a].z - kRingU[a].z * kRingV[a].y == kAxis[a].x
        && kRingU[a].z * kRingV[a].x - kRingU[a].x * kRingV[a].z == kAxis[a].y
        && kRingU[a].x * kRingV[a].y - kRingU[a].y * kRingV[a].x == kAxis[a].z;
}
static_assert(RingBasisMatchesAxis(0) && RingBasisMatchesAxis(1) && RingBasisMatchesAxis(2),
              "回転ギズモの輪の基準ベクトル(U,V)が軸と食い違っています");

inline float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vector3 AddScaled(const Vector3& p, const Vector3& dir, float s) {
    return { p.x + dir.x * s, p.y + dir.y * s, p.z + dir.z * s };
}

// 値を一定の刻みに揃える。stepが0以下なら何もしない
inline float SnapTo(float value, float step) {
    if (step <= 0.0f) { return value; }
    return roundf(value / step) * step;
}

// 角度を -π〜π に畳む。輪を回し続けたとき、±180度をまたいだ瞬間に1回転ぶん飛ぶのを防ぐ
inline float WrapAngle(float angle) {
    constexpr float kPi = 3.14159265359f;
    while (angle > kPi) { angle -= kPi * 2.0f; }
    while (angle < -kPi) { angle += kPi * 2.0f; }
    return angle;
}

// ワールド座標 → 画面のピクセル座標。カメラの後ろならfalse
inline bool ProjectToScreen(const Vector3& p, const Matrix4x4& vp, float width, float height, Vector2& out) {
    float x = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
    float y = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
    float w = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
    if (w <= 1e-5f) { return false; }
    out.x = (x / w * 0.5f + 0.5f) * width;
    out.y = (1.0f - (y / w * 0.5f + 0.5f)) * height;
    return true;
}

// 点pと線分abの最短距離（画面の上で、軸を掴めているかの判定に使う）
inline float DistancePointToSegment(const Vector2& p, const Vector2& a, const Vector2& b) {
    Vector2 ab{ b.x - a.x, b.y - a.y };
    float lenSq = ab.x * ab.x + ab.y * ab.y;
    float t = 0.0f;
    if (lenSq > 1e-6f) {
        t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq;
        t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    float dx = p.x - (a.x + ab.x * t);
    float dy = p.y - (a.y + ab.y * t);
    return sqrtf(dx * dx + dy * dy);
}

// 軸（axisOrigin起点・axisDir方向の直線）の上で、マウスのレイに一番近い点のパラメータt。
// マウスを動かした量が、そのまま軸方向の移動量になる（Unityの移動ギズモと同じ操作感）
inline float ClosestTOnAxis(const Vector3& axisOrigin, const Vector3& axisDir,
                            const Vector3& rayOrigin, const Vector3& rayDir) {
    Vector3 r{ axisOrigin.x - rayOrigin.x, axisOrigin.y - rayOrigin.y, axisOrigin.z - rayOrigin.z };
    float b = Dot(axisDir, rayDir);
    float c = Dot(axisDir, r);
    float f = Dot(rayDir, r);
    float denom = 1.0f - b * b;
    if (fabsf(denom) < 1e-6f) { return 0.0f; } // 軸を真正面から見ている
    return (b * f - c) / denom;
}

// レイと平面の交点。平面とほぼ平行（輪を真横から見ている）ならfalse
inline bool RayPlaneIntersect(const Vector3& rayOrigin, const Vector3& rayDir,
                              const Vector3& planePoint, const Vector3& planeNormal, Vector3& out) {
    float denom = Dot(rayDir, planeNormal);
    if (fabsf(denom) < 1e-4f) { return false; }
    Vector3 diff{ planePoint.x - rayOrigin.x, planePoint.y - rayOrigin.y, planePoint.z - rayOrigin.z };
    float t = Dot(diff, planeNormal) / denom;
    if (t < 0.0f) { return false; }
    out = AddScaled(rayOrigin, rayDir, t);
    return true;
}

// 輪の上の、角度angleの点
inline Vector3 PointOnRing(const Vector3& center, int axis, float radius, float angle) {
    float c = cosf(angle) * radius;
    float s = sinf(angle) * radius;
    return {
        center.x + kRingU[axis].x * c + kRingV[axis].x * s,
        center.y + kRingU[axis].y * c + kRingV[axis].y * s,
        center.z + kRingU[axis].z * c + kRingV[axis].z * s,
    };
}

// 輪の上での角度（PointOnRingの逆）
inline float AngleOnRing(const Vector3& center, int axis, const Vector3& point) {
    Vector3 d{ point.x - center.x, point.y - center.y, point.z - center.z };
    return atan2f(Dot(d, kRingV[axis]), Dot(d, kRingU[axis]));
}

} // namespace Gizmo
