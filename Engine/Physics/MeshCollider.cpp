#include "MeshCollider.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace {
    // セル座標(x,y,z)を1つのキーにパックする（21bitずつ、負の座標も扱えるようbiasを足す）
    int64_t CellKey(int x, int y, int z) {
        const int64_t bias = 1 << 20;
        const int64_t mask = (1 << 21) - 1;
        return ((int64_t)(x + bias) & mask) | (((int64_t)(y + bias) & mask) << 21) | (((int64_t)(z + bias) & mask) << 42);
    }

    // 三角形abc上で、点pに一番近い点を求める（Ericson "Real-Time Collision Detection" のアルゴリズム）
    Vector3 ClosestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c) {
        Vector3 ab = Sub(b, a);
        Vector3 ac = Sub(c, a);
        Vector3 ap = Sub(p, a);

        float d1 = Dot(ab, ap);
        float d2 = Dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) { return a; } // 頂点aの外側の領域

        Vector3 bp = Sub(p, b);
        float d3 = Dot(ab, bp);
        float d4 = Dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) { return b; } // 頂点bの外側の領域

        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float v = d1 / (d1 - d3);
            return Add(a, Scale(ab, v)); // 辺abの上
        }

        Vector3 cp = Sub(p, c);
        float d5 = Dot(ab, cp);
        float d6 = Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) { return c; } // 頂点cの外側の領域

        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            float w = d2 / (d2 - d6);
            return Add(a, Scale(ac, w)); // 辺acの上
        }

        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return Add(b, Scale(Sub(c, b), w)); // 辺bcの上
        }

        // 三角形の内部
        float denom = 1.0f / (va + vb + vc);
        float v = vb * denom;
        float w = vc * denom;
        return Add(a, Add(Scale(ab, v), Scale(ac, w)));
    }
}

std::vector<Triangle> BuildWorldTriangles(const std::vector<Vector3>& localPositions, const std::vector<uint32_t>& indices, const Matrix4x4& worldMatrix) {
    std::vector<Triangle> triangles;
    triangles.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Triangle tri;
        tri.a = TransformPoint(localPositions[indices[i + 0]], worldMatrix);
        tri.b = TransformPoint(localPositions[indices[i + 1]], worldMatrix);
        tri.c = TransformPoint(localPositions[indices[i + 2]], worldMatrix);
        triangles.push_back(tri);
    }
    return triangles;
}

TriangleGrid BuildTriangleGrid(const std::vector<Triangle>& triangles) {
    TriangleGrid grid;
    if (triangles.empty()) { return grid; }

    Vector3 meshMin{ (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
    Vector3 meshMax{ -(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)() };
    for (const auto& tri : triangles) {
        for (const Vector3* p : { &tri.a, &tri.b, &tri.c }) {
            meshMin.x = (std::min)(meshMin.x, p->x); meshMin.y = (std::min)(meshMin.y, p->y); meshMin.z = (std::min)(meshMin.z, p->z);
            meshMax.x = (std::max)(meshMax.x, p->x); meshMax.y = (std::max)(meshMax.y, p->y); meshMax.z = (std::max)(meshMax.z, p->z);
        }
    }

    // メッシュ全体の大きさと三角形数から、セルあたり平均1三角形程度になるようセルサイズを見積もる
    float diagonal = Length(Sub(meshMax, meshMin));
    float cellSize = diagonal / std::cbrt(static_cast<float>(triangles.size()) + 1.0f);
    grid.cellSize = (std::max)(0.05f, cellSize);

    for (uint32_t i = 0; i < triangles.size(); ++i) {
        const Triangle& tri = triangles[i];
        Vector3 triMin{ (std::min)({ tri.a.x, tri.b.x, tri.c.x }), (std::min)({ tri.a.y, tri.b.y, tri.c.y }), (std::min)({ tri.a.z, tri.b.z, tri.c.z }) };
        Vector3 triMax{ (std::max)({ tri.a.x, tri.b.x, tri.c.x }), (std::max)({ tri.a.y, tri.b.y, tri.c.y }), (std::max)({ tri.a.z, tri.b.z, tri.c.z }) };

        int x0 = (int)std::floor(triMin.x / grid.cellSize), x1 = (int)std::floor(triMax.x / grid.cellSize);
        int y0 = (int)std::floor(triMin.y / grid.cellSize), y1 = (int)std::floor(triMax.y / grid.cellSize);
        int z0 = (int)std::floor(triMin.z / grid.cellSize), z1 = (int)std::floor(triMax.z / grid.cellSize);
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    grid.cells[CellKey(x, y, z)].push_back(i);
                }
            }
        }
    }
    return grid;
}

void QueryTriangleGrid(const TriangleGrid& grid, const Vector3& center, float radius, std::vector<uint32_t>& outCandidates) {
    outCandidates.clear();
    if (grid.cells.empty()) { return; }

    int x0 = (int)std::floor((center.x - radius) / grid.cellSize), x1 = (int)std::floor((center.x + radius) / grid.cellSize);
    int y0 = (int)std::floor((center.y - radius) / grid.cellSize), y1 = (int)std::floor((center.y + radius) / grid.cellSize);
    int z0 = (int)std::floor((center.z - radius) / grid.cellSize), z1 = (int)std::floor((center.z + radius) / grid.cellSize);
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                auto it = grid.cells.find(CellKey(x, y, z));
                if (it != grid.cells.end()) {
                    outCandidates.insert(outCandidates.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }
}

bool ResolveSphereVsMesh(Vector3& center, float radius, const std::vector<Triangle>& triangles, const TriangleGrid& grid, Vector3& outPushedNormal, Vector3& outContactPoint) {
    bool collided = false;
    float deepestPenetration = 0.0f;
    Vector3 bestNormal{ 0.0f, 1.0f, 0.0f };
    Vector3 bestContact{};

    static thread_local std::vector<uint32_t> candidates;
    QueryTriangleGrid(grid, center, radius, candidates);

    for (uint32_t idx : candidates) {
        const Triangle& tri = triangles[idx];
        Vector3 closest = ClosestPointOnTriangle(center, tri.a, tri.b, tri.c);
        Vector3 diff = Sub(center, closest);
        float dist = Length(diff);
        if (dist >= radius) { continue; }

        float penetration = radius - dist;
        Vector3 normal;
        if (dist > 1e-5f) {
            normal = Scale(diff, 1.0f / dist);
        } else {
            // 球の中心がちょうど三角形上（またはその直下）にある特殊ケースは、三角形の法線を使う
            normal = Normalize(Cross(Sub(tri.b, tri.a), Sub(tri.c, tri.a)));
        }

        if (penetration > deepestPenetration) {
            deepestPenetration = penetration;
            bestNormal = normal;
            bestContact = closest;
            collided = true;
        }
    }

    if (collided) {
        center = Add(center, Scale(bestNormal, deepestPenetration));
        outPushedNormal = bestNormal;
        outContactPoint = bestContact;
    }
    return collided;
}

// 複雑な形状同士（スザンヌ対スザンヌなど）の当たり判定：
// 「動く側」の実頂点群を、それぞれ半径vertexRadiusの小さな球とみなし、「相手」の三角形群に対して
// ResolveSphereVsMeshと同じロジックを頂点ごとに適用する。最も深くめり込んでいた頂点1つの結果を
// オブジェクト全体の押し出し量として採用する（剛体を1点の接触で近似的に押し戻す、簡易的な手法）。
// 各頂点ごとにgridで近傍三角形だけへ絞り込むことで、頂点数×全三角形数の総当たりを避ける
bool ResolveVerticesVsMesh(const std::vector<Vector3>& worldVertices, float vertexRadius, const std::vector<Triangle>& triangles, const TriangleGrid& grid, Vector3& outPushOffset, Vector3& outPushedNormal, Vector3& outContactPoint) {
    bool collided = false;
    float deepestPenetration = 0.0f;
    Vector3 bestNormal{ 0.0f, 1.0f, 0.0f };
    Vector3 bestContact{};

    static thread_local std::vector<uint32_t> candidates;
    for (const auto& v : worldVertices) {
        QueryTriangleGrid(grid, v, vertexRadius, candidates);
        for (uint32_t idx : candidates) {
            const Triangle& tri = triangles[idx];
            Vector3 closest = ClosestPointOnTriangle(v, tri.a, tri.b, tri.c);
            Vector3 diff = Sub(v, closest);
            float dist = Length(diff);
            if (dist >= vertexRadius) { continue; }

            float penetration = vertexRadius - dist;
            Vector3 normal;
            if (dist > 1e-5f) {
                normal = Scale(diff, 1.0f / dist);
            } else {
                normal = Normalize(Cross(Sub(tri.b, tri.a), Sub(tri.c, tri.a)));
            }

            if (penetration > deepestPenetration) {
                deepestPenetration = penetration;
                bestNormal = normal;
                bestContact = closest;
                collided = true;
            }
        }
    }

    if (collided) {
        outPushOffset = Scale(bestNormal, deepestPenetration);
        outPushedNormal = bestNormal;
        outContactPoint = bestContact;
    }
    return collided;
}
