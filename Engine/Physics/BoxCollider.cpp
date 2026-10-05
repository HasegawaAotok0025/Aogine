#include "BoxCollider.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <utility>

AABB ComputeLocalAABB(const std::vector<Vector3>& positions) {
    AABB aabb;
    if (positions.empty()) { return aabb; }

    aabb.min = { (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
    aabb.max = { -(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)() };

    for (const auto& p : positions) {
        aabb.min.x = (std::min)(aabb.min.x, p.x);
        aabb.min.y = (std::min)(aabb.min.y, p.y);
        aabb.min.z = (std::min)(aabb.min.z, p.z);
        aabb.max.x = (std::max)(aabb.max.x, p.x);
        aabb.max.y = (std::max)(aabb.max.y, p.y);
        aabb.max.z = (std::max)(aabb.max.z, p.z);
    }
    return aabb;
}

namespace {
    float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Vector3 Scale(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
    float Length(const Vector3& v) { return sqrtf(Dot(v, v)); }
    // 点のワールド変換にはMathUtility.hのTransformPoint（共通の公開関数）を使う
    // （以前はここにファイルローカル版を持っていたが、公開版と同名・同シグネチャで曖昧になるため削除した）
}

OBB TransformToOBB(const AABB& localAABB, const Matrix4x4& worldMatrix) {
    OBB obb;

    Vector3 localCenter = {
        (localAABB.min.x + localAABB.max.x) * 0.5f,
        (localAABB.min.y + localAABB.max.y) * 0.5f,
        (localAABB.min.z + localAABB.max.z) * 0.5f,
    };
    Vector3 localHalf = {
        (localAABB.max.x - localAABB.min.x) * 0.5f,
        (localAABB.max.y - localAABB.min.y) * 0.5f,
        (localAABB.max.z - localAABB.min.z) * 0.5f,
    };

    // ワールド行列の各行は「ローカルのX/Y/Z軸がワールド空間でどちらを向き、どれだけ伸びているか」を表す
    // （回転成分＋スケール成分。MakeAffineMatrixがSRTをこの順で合成しているため、行の長さ＝スケールになる）
    Vector3 rowX = { worldMatrix.m[0][0], worldMatrix.m[0][1], worldMatrix.m[0][2] };
    Vector3 rowY = { worldMatrix.m[1][0], worldMatrix.m[1][1], worldMatrix.m[1][2] };
    Vector3 rowZ = { worldMatrix.m[2][0], worldMatrix.m[2][1], worldMatrix.m[2][2] };

    float scaleX = Length(rowX);
    float scaleY = Length(rowY);
    float scaleZ = Length(rowZ);

    obb.axis[0] = (scaleX > 1e-8f) ? Scale(rowX, 1.0f / scaleX) : Vector3{ 1.0f, 0.0f, 0.0f };
    obb.axis[1] = (scaleY > 1e-8f) ? Scale(rowY, 1.0f / scaleY) : Vector3{ 0.0f, 1.0f, 0.0f };
    obb.axis[2] = (scaleZ > 1e-8f) ? Scale(rowZ, 1.0f / scaleZ) : Vector3{ 0.0f, 0.0f, 1.0f };

    obb.halfSize = { localHalf.x * scaleX, localHalf.y * scaleY, localHalf.z * scaleZ };
    obb.center = TransformPoint(localCenter, worldMatrix);

    return obb;
}

Matrix4x4 MakeOBBWorldMatrix(const OBB& obb) {
    // 0〜1のユニットボックス（ローカル原点(0,0,0)〜(1,1,1)）を、OBBの向き・大きさ・位置に変形する行列
    Matrix4x4 mat{};
    Vector3 ex = Scale(obb.axis[0], obb.halfSize.x * 2.0f);
    Vector3 ey = Scale(obb.axis[1], obb.halfSize.y * 2.0f);
    Vector3 ez = Scale(obb.axis[2], obb.halfSize.z * 2.0f);
    mat.m[0][0] = ex.x; mat.m[0][1] = ex.y; mat.m[0][2] = ex.z;
    mat.m[1][0] = ey.x; mat.m[1][1] = ey.y; mat.m[1][2] = ey.z;
    mat.m[2][0] = ez.x; mat.m[2][1] = ez.y; mat.m[2][2] = ez.z;

    // ユニットボックスの原点(0,0,0)がOBBの「最小コーナー」に来るように平行移動する
    Vector3 corner = Sub(Sub(Sub(obb.center, Scale(obb.axis[0], obb.halfSize.x)),
                                   Scale(obb.axis[1], obb.halfSize.y)),
                               Scale(obb.axis[2], obb.halfSize.z));
    mat.m[3][0] = corner.x; mat.m[3][1] = corner.y; mat.m[3][2] = corner.z; mat.m[3][3] = 1.0f;
    return mat;
}

// 指定した軸へOBBを投影したときの半径（中心から見た広がり）
static float ProjectedRadius(const OBB& box, const Vector3& axis) {
    return fabsf(Dot(box.axis[0], axis)) * box.halfSize.x
         + fabsf(Dot(box.axis[1], axis)) * box.halfSize.y
         + fabsf(Dot(box.axis[2], axis)) * box.halfSize.z;
}

// OBB同士の分離軸判定（SAT）本体。
// 15軸すべてを調べ、最も浅いめり込みの軸と「どういう当たり方か」を求める
bool ComputeSAT(const OBB& a, const OBB& b, SATResult& out) {
    Vector3 T = Sub(b.center, a.center);

    float bestOverlap = (std::numeric_limits<float>::max)();
    Vector3 bestAxis{ 0.0f, 1.0f, 0.0f };
    SATFeature bestFeature = SATFeature::FaceA;
    int bestI = 0, bestJ = 0;

    // --- 面法線（aの3軸 ＋ bの3軸）---
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 3; ++i) {
            Vector3 L = (side == 0) ? a.axis[i] : b.axis[i];
            float overlap = ProjectedRadius(a, L) + ProjectedRadius(b, L) - fabsf(Dot(T, L));
            if (overlap < 0.0f) { return false; } // この軸で離れている＝衝突していない
            if (overlap < bestOverlap) {
                bestOverlap = overlap;
                bestAxis = L;
                bestFeature = (side == 0) ? SATFeature::FaceA : SATFeature::FaceB;
                if (side == 0) { bestI = i; } else { bestJ = i; }
            }
        }
    }

    // --- 辺どうしの外積（3×3）---
    // 面の軸より「わずかに浅い」程度で辺の軸が勝つと、ほんの少し姿勢が揺れただけで
    // 接触点の作り方が面用⇔辺用に何度も切り替わり、接している物がガタつく。
    // 面をわずかに優遇して、明確に辺の方が浅いときだけ辺として扱う（物理エンジンでよく使われる措置）
    const float kFaceBias = 1.02f;

    // 辺の軸のうち最も浅いものは、面に負けた場合でも別枠で覚えておく。
    // 頂点がどこにも入っていないと分かっている場面では、優遇なしのこの値が必要になる
    float bestEdgeOverlap = (std::numeric_limits<float>::max)();
    Vector3 bestEdgeAxis{ 0.0f, 1.0f, 0.0f };
    int bestEdgeI = 0, bestEdgeJ = 0;
    bool hasEdge = false;

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 c = Cross(a.axis[i], b.axis[j]);
            float len = Length(c);
            if (len < 1e-6f) { continue; } // 辺どうしがほぼ平行。この軸は判定に使えない
            Vector3 L = Scale(c, 1.0f / len);

            float overlap = ProjectedRadius(a, L) + ProjectedRadius(b, L) - fabsf(Dot(T, L));
            if (overlap < 0.0f) { return false; }

            if (overlap < bestEdgeOverlap) {
                bestEdgeOverlap = overlap;
                bestEdgeAxis = L;
                bestEdgeI = i;
                bestEdgeJ = j;
                hasEdge = true;
            }
            if (overlap * kFaceBias < bestOverlap) {
                bestOverlap = overlap;
                bestAxis = L;
                bestFeature = SATFeature::EdgeEdge;
                bestI = i;
                bestJ = j;
            }
        }
    }

    // 軸の向きは外積やボックスの軸の取り方で符号が定まらないので、
    // 必ず「aからbへ向かう向き」に揃えておく。これで呼び出し側が毎回符号を悩まずに済む
    if (Dot(T, bestAxis) < 0.0f) { bestAxis = Scale(bestAxis, -1.0f); }
    if (Dot(T, bestEdgeAxis) < 0.0f) { bestEdgeAxis = Scale(bestEdgeAxis, -1.0f); }

    out.axis = bestAxis;
    out.penetration = bestOverlap;
    out.feature = bestFeature;
    out.indexA = bestI;
    out.indexB = bestJ;

    out.hasEdgeAxis = hasEdge;
    out.edgeAxis = bestEdgeAxis;
    out.edgePenetration = bestEdgeOverlap;
    out.edgeIndexA = bestEdgeI;
    out.edgeIndexB = bestEdgeJ;
    return true;
}

bool Intersect(const OBB& a, const OBB& b) {
    SATResult result;
    return ComputeSAT(a, b, result);
}

bool ComputeMTV(const OBB& a, const OBB& b, Vector3& mtv) {
    SATResult result;
    if (!ComputeSAT(a, b, result)) { return false; }

    // ComputeSATの軸は「aからbへ向かう向き」なので、aを遠ざけるには逆向きに押す
    mtv = Scale(result.axis, -result.penetration);
    return true;
}

// 2本の線分の最接近点。辺どうしが斜めに交差しているときの接触点を求めるのに使う
// （交差といっても、実際には最も近づいている1点で触れていると考える）
void ClosestPointsBetweenSegments(const Vector3& p1, const Vector3& q1,
                                  const Vector3& p2, const Vector3& q2,
                                  Vector3& outOn1, Vector3& outOn2) {
    auto Saturate = [](float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); };

    Vector3 d1 = Sub(q1, p1); // 線分1の向きと長さ
    Vector3 d2 = Sub(q2, p2); // 線分2の向きと長さ
    Vector3 r = Sub(p1, p2);
    float a = Dot(d1, d1);
    float e = Dot(d2, d2);
    float f = Dot(d2, r);

    const float kEpsilon = 1e-8f;
    float s = 0.0f, t = 0.0f;

    if (a <= kEpsilon && e <= kEpsilon) {
        // 両方とも長さがない（点と点）
        outOn1 = p1;
        outOn2 = p2;
        return;
    }
    if (a <= kEpsilon) {
        t = Saturate(f / e); // 線分1が点なので、線分2の上を探すだけ
    } else {
        float c = Dot(d1, r);
        if (e <= kEpsilon) {
            s = Saturate(-c / a); // 線分2が点
        } else {
            float b = Dot(d1, d2);
            float denom = a * e - b * b; // 0なら2本が平行
            s = (denom > kEpsilon) ? Saturate((b * f - c * e) / denom) : 0.0f;
            t = (b * s + f) / e;
            // 求めたtが線分の外に出た場合は端点に留め、その位置で改めてsを求め直す
            if (t < 0.0f) { t = 0.0f; s = Saturate(-c / a); }
            else if (t > 1.0f) { t = 1.0f; s = Saturate((b - c) / a); }
        }
    }

    outOn1 = Add(p1, Scale(d1, s));
    outOn2 = Add(p2, Scale(d2, t));
}

// 辺どうしが交差しているとき、実際に触れている2本の辺を求めて最接近点を返す。
//
// 分離軸が a.axis[i] × b.axis[j] だったということは、
// 触れているのはaの「i軸に平行な辺」とbの「j軸に平行な辺」。
// 12本ある辺のうちどれかは、軸方向で最も相手に近い側を選べば決まる
// （i軸に沿った方向はどこも同じ高さなので、残り2軸の符号だけで1本に絞れる）
void FindContactEdgePoints(const OBB& a, const OBB& b, const SATResult& sat,
                           Vector3& outPointOnA, Vector3& outPointOnB) {
    // 面の優遇が入っていない、辺だけで見た最良の軸を使う
    const Vector3& axis = sat.edgeAxis;
    const int indexA = sat.edgeIndexA;
    const int indexB = sat.edgeIndexB;

    const float halfA[3] = { a.halfSize.x, a.halfSize.y, a.halfSize.z };
    const float halfB[3] = { b.halfSize.x, b.halfSize.y, b.halfSize.z };

    // aの辺：bのある向き（+axis）へ寄せる
    Vector3 baseA = a.center;
    for (int k = 0; k < 3; ++k) {
        if (k == indexA) { continue; } // 辺に沿った軸は自由。このあと±halfで両端を作る
        float sign = (Dot(a.axis[k], axis) >= 0.0f) ? 1.0f : -1.0f;
        baseA = Add(baseA, Scale(a.axis[k], halfA[k] * sign));
    }

    // bの辺：aのある向き（-axis）へ寄せる
    Vector3 baseB = b.center;
    for (int k = 0; k < 3; ++k) {
        if (k == indexB) { continue; }
        float sign = (Dot(b.axis[k], axis) >= 0.0f) ? -1.0f : 1.0f;
        baseB = Add(baseB, Scale(b.axis[k], halfB[k] * sign));
    }

    Vector3 edgeA = Scale(a.axis[indexA], halfA[indexA]);
    Vector3 edgeB = Scale(b.axis[indexB], halfB[indexB]);

    ClosestPointsBetweenSegments(Sub(baseA, edgeA), Add(baseA, edgeA),
                                 Sub(baseB, edgeB), Add(baseB, edgeB),
                                 outPointOnA, outPointOnB);
}

namespace {
    // OBB上で、指定方向へ最も遠い点（支持点）。各軸で「その向きと同じなら+半径、逆なら-半径」を選ぶだけ
    Vector3 SupportPoint(const OBB& obb, const Vector3& dir) {
        const float half[3] = { obb.halfSize.x, obb.halfSize.y, obb.halfSize.z };
        Vector3 result = obb.center;
        for (int i = 0; i < 3; ++i) {
            result = Add(result, Scale(obb.axis[i], (Dot(obb.axis[i], dir) >= 0.0f) ? half[i] : -half[i]));
        }
        return result;
    }

    // 多角形を1枚の平面で切り取る（Sutherland-Hodgman法）。
    // Dot(planeNormal, p) <= planeOffset を満たす側だけを残し、
    // 辺が平面をまたぐところには新しい頂点を作って切り口をふさぐ
    int ClipPolygonAgainstPlane(const Vector3* in, int inCount,
                                const Vector3& planeNormal, float planeOffset, Vector3* out) {
        int outCount = 0;
        for (int i = 0; i < inCount; ++i) {
            const Vector3& cur = in[i];
            const Vector3& next = in[(i + 1) % inCount];
            float dCur = Dot(planeNormal, cur) - planeOffset;
            float dNext = Dot(planeNormal, next) - planeOffset;

            if (dCur <= 0.0f) { out[outCount++] = cur; }              // 残す側にある頂点
            if ((dCur > 0.0f) != (dNext > 0.0f)) {                    // この辺が平面をまたいだ
                float t = dCur / (dCur - dNext);
                out[outCount++] = Add(cur, Scale(Sub(next, cur), t)); // 交点を新しい頂点にする
            }
        }
        return outCount;
    }
}

int BuildContactManifold(const OBB& a, const OBB& b, const SATResult& sat,
                         Vector3 outPoints[kMaxManifoldPoints],
                         float outDepths[kMaxManifoldPoints],
                         Vector3& outNormal)
{
    // --- 辺どうしが斜めに交差している場合 ---
    // 触れているのは1点だけなので、2本の辺の最接近点をそのまま接触点にする
    if (sat.feature == SATFeature::EdgeEdge && sat.hasEdgeAxis) {
        Vector3 pa, pb;
        FindContactEdgePoints(a, b, sat, pa, pb);
        outPoints[0] = Scale(Add(pa, pb), 0.5f);
        outDepths[0] = sat.edgePenetration;
        outNormal = Scale(sat.edgeAxis, -1.0f); // edgeAxisはa→b向きなので、aを押し出すには反転
        return 1;
    }

    // --- 面が接触面の場合 ---
    // めり込みが最も浅かった面を「基準面」とし、相手の面を基準面の縁で切り取る
    bool referenceIsA = (sat.feature == SATFeature::FaceA);
    const OBB& ref = referenceIsA ? a : b;   // 基準となる面を持つ箱
    const OBB& inc = referenceIsA ? b : a;   // 切り取られる側の箱
    int refIndex = referenceIsA ? sat.indexA : sat.indexB;

    const float refHalf[3] = { ref.halfSize.x, ref.halfSize.y, ref.halfSize.z };
    const float incHalf[3] = { inc.halfSize.x, inc.halfSize.y, inc.halfSize.z };

    // 基準面の法線。6面のうち「相手のいる側」を向いた面を選ぶ
    Vector3 toInc = Sub(inc.center, ref.center);
    float refSign = (Dot(toInc, ref.axis[refIndex]) >= 0.0f) ? 1.0f : -1.0f;
    Vector3 n = Scale(ref.axis[refIndex], refSign); // refからincへ向かう向き
    Vector3 refFaceCenter = Add(ref.center, Scale(n, refHalf[refIndex]));
    int refT0 = (refIndex + 1) % 3;
    int refT1 = (refIndex + 2) % 3;

    // 相手側の面は、基準面の法線に最も正面から向き合っている面を選ぶ
    int incIndex = 0;
    float bestAlignment = -1.0f;
    for (int k = 0; k < 3; ++k) {
        float alignment = fabsf(Dot(inc.axis[k], n));
        if (alignment > bestAlignment) { bestAlignment = alignment; incIndex = k; }
    }
    float incSign = (Dot(inc.axis[incIndex], n) >= 0.0f) ? -1.0f : 1.0f; // nと逆を向く面
    Vector3 incFaceCenter = Add(inc.center, Scale(inc.axis[incIndex], incHalf[incIndex] * incSign));
    int incT0 = (incIndex + 1) % 3;
    int incT1 = (incIndex + 2) % 3;

    // 相手の面の4隅
    Vector3 incU = Scale(inc.axis[incT0], incHalf[incT0]);
    Vector3 incV = Scale(inc.axis[incT1], incHalf[incT1]);
    Vector3 polygon[kMaxManifoldPoints * 2];
    Vector3 scratch[kMaxManifoldPoints * 2];
    polygon[0] = Add(Add(incFaceCenter, incU), incV);
    polygon[1] = Add(Sub(incFaceCenter, incU), incV);
    polygon[2] = Sub(Sub(incFaceCenter, incU), incV);
    polygon[3] = Sub(Add(incFaceCenter, incU), incV);
    int count = 4;

    // 基準面の4辺（横2枚・縦2枚の平面）で順に切り取っていく
    const int tangents[2] = { refT0, refT1 };
    for (int side = 0; side < 2; ++side) {
        const Vector3& t = ref.axis[tangents[side]];
        float half = refHalf[tangents[side]];
        float center = Dot(t, refFaceCenter);

        count = ClipPolygonAgainstPlane(polygon, count, t, center + half, scratch);
        if (count == 0) { break; }
        count = ClipPolygonAgainstPlane(scratch, count, Scale(t, -1.0f), -center + half, polygon);
        if (count == 0) { break; }
    }

    // 基準面より内側（めり込んでいる側）にある頂点だけを接触点として採用する
    int result = 0;
    for (int i = 0; i < count && result < kMaxManifoldPoints; ++i) {
        float depth = -Dot(Sub(polygon[i], refFaceCenter), n); // 正なら基準面より内側
        if (depth < 0.0f) { continue; }
        outPoints[result] = polygon[i];
        outDepths[result] = depth;
        result++;
    }

    outNormal = referenceIsA ? Scale(n, -1.0f) : n; // 常に「aを押し出す向き」に揃える

    // 触れるか触れないかの浅い接触では、丸め誤差で1点も残らないことがある。
    // 取りこぼして素通りさせるより、支持点で1点だけ作っておく方が確実（最後の保険）
    if (result == 0) {
        outPoints[0] = SupportPoint(a, sat.axis);
        outDepths[0] = sat.penetration;
        result = 1;
    }
    return result;
}

// 球とOBBのブロードフェーズ判定：OBB上で球の中心に最も近い点を求め、その距離が半径以下かどうかを見るだけの軽量判定
// （三角形単位のメッシュ判定のように全ポリゴンを調べる必要がないため、メッシュコライダーの事前絞り込みに向いている）
bool IntersectSphereOBB(const Vector3& center, float radius, const OBB& obb) {
    Vector3 d = Sub(center, obb.center);
    float halfSizes[3] = { obb.halfSize.x, obb.halfSize.y, obb.halfSize.z };

    Vector3 closest = obb.center;
    for (int i = 0; i < 3; i++) {
        float dist = Dot(d, obb.axis[i]);
        dist = (std::max)(-halfSizes[i], (std::min)(halfSizes[i], dist)); // OBBの範囲内にクランプ
        closest = Add(closest, Scale(obb.axis[i], dist));
    }

    Vector3 diff = Sub(center, closest);
    return Dot(diff, diff) <= radius * radius;
}

// レイ×OBBのスラブ法による交差判定。OBBの3軸それぞれをスラブ（平行な2平面に挟まれた領域）とみなし、
// レイが全スラブに同時に入っている区間[tMin, tMax]が存在すれば交差している
bool RayIntersectOBB(const Vector3& rayOrigin, const Vector3& rayDir, const OBB& obb, float& outDistance) {
    float tMin = 0.0f;
    float tMax = (std::numeric_limits<float>::max)();
    Vector3 p = Sub(obb.center, rayOrigin);
    float halfSizes[3] = { obb.halfSize.x, obb.halfSize.y, obb.halfSize.z };

    for (int i = 0; i < 3; i++) {
        const Vector3& axis = obb.axis[i];
        float e = Dot(axis, p);
        float f = Dot(axis, rayDir);

        if (fabsf(f) > 1e-8f) {
            float t1 = (e + halfSizes[i]) / f;
            float t2 = (e - halfSizes[i]) / f;
            if (t1 > t2) { std::swap(t1, t2); }
            if (t1 > tMin) { tMin = t1; }
            if (t2 < tMax) { tMax = t2; }
            if (tMin > tMax) { return false; }
        } else {
            // レイがこのスラブとほぼ平行：原点がスラブの外側にあれば絶対に交差しない
            if (-e - halfSizes[i] > 0.0f || -e + halfSizes[i] < 0.0f) { return false; }
        }
    }

    outDistance = tMin;
    return true;
}
