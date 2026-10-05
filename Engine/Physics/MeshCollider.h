#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "../Core/MathUtility.h"

// メッシュコライダー用の三角形（ワールド空間の3頂点）
struct Triangle {
    Vector3 a, b, c;
};

// ローカル頂点位置とインデックス列（3個ずつで1三角形）から、ワールド空間の三角形リストを作る
// 静止オブジェクト用の想定なので、通常はそのオブジェクトが動かない限り作り直す必要はない
std::vector<Triangle> BuildWorldTriangles(const std::vector<Vector3>& localPositions, const std::vector<uint32_t>& indices, const Matrix4x4& worldMatrix);

// 三角形群を空間分割した一様グリッド（ハッシュグリッド）。
// スザンヌ（約1000三角形）に対して「動く側」の頂点1つ1つを総当たりすると
// 頂点数×三角形数のオーダーになり非常に重いため、三角形をあらかじめセルに振り分けておき、
// 判定したい点の近くのセルだけを見れば済むようにする（近傍探索による計算範囲の限定）。
// 三角形のワールド座標が変わらない限り作り直す必要はない（Engine::UpdatePhysics側でキャッシュする）
struct TriangleGrid {
    float cellSize = 0.5f;
    std::unordered_map<int64_t, std::vector<uint32_t>> cells; // セル座標→そのセルに重なる三角形インデックス
};

// 三角形リストからグリッドを構築する
TriangleGrid BuildTriangleGrid(const std::vector<Triangle>& triangles);

// center（半径radius）が重なる可能性のあるセルから、候補三角形インデックスをoutCandidatesへ集める
// （複数セルにまたがる三角形は重複して入ることがあるが、判定自体は重複しても結果は変わらないため許容する）
void QueryTriangleGrid(const TriangleGrid& grid, const Vector3& center, float radius, std::vector<uint32_t>& outCandidates);

// 球とメッシュ（三角形の集合）の衝突を判定する。gridで近傍三角形だけに絞り込んでから判定する
// めり込みが一番深い三角形を基準にcenterを押し出し、衝突していればtrueを返す
// outPushedNormalには押し出した方向（正規化済み）、outContactPointには接触点（三角形上の最近接点）が入る
bool ResolveSphereVsMesh(Vector3& center, float radius, const std::vector<Triangle>& triangles, const TriangleGrid& grid, Vector3& outPushedNormal, Vector3& outContactPoint);

// 「動く側」の実際のメッシュ頂点群（ワールド空間）を、「相手」のメッシュ（三角形の集合、ワールド空間）に対して押し出す
// 各頂点をvertexRadiusの半径を持つ小さな球とみなし、それぞれ最近接の三角形との距離を調べ、
// 最も深くめり込んでいる頂点1つを基準に押し出し量・法線を決める（GJK/EPAのような厳密な凸包判定ではなく、
// 頂点サンプリングによる近似だが、スザンヌ同士のような凹凸のある複雑な形状同士でも
// 単純な球/箱よりずっと実際の見た目に近い形で当たり判定ができる）。gridで各頂点の近傍三角形だけに絞り込む
// 戻り値はtrue=めり込みあり（このときのみoutPushOffset/outPushedNormal/outContactPointに値を書き込む）
bool ResolveVerticesVsMesh(const std::vector<Vector3>& worldVertices, float vertexRadius, const std::vector<Triangle>& triangles, const TriangleGrid& grid, Vector3& outPushOffset, Vector3& outPushedNormal, Vector3& outContactPoint);
