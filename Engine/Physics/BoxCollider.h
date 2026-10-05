#pragma once
#include <vector>
#include "../Core/MathUtility.h"

// 軸平行境界ボックス（Axis-Aligned Bounding Box）
// このプロジェクトでは「モデルのローカル空間での素の頂点範囲」を保持するためだけに使う
// （ワールド空間での当たり判定・描画には、回転に追従するOBBを使う）
struct AABB {
    Vector3 min{ 0.0f, 0.0f, 0.0f };
    Vector3 max{ 0.0f, 0.0f, 0.0f };
};

// 有向境界ボックス（Oriented Bounding Box）
// Unityの BoxCollider と同じ考え方で、オブジェクトの回転にそのまま追従するボックスコライダー。
// 「AABBを毎フレーム外接直方体で作り直す」旧方式だと、回転させたときに見た目より大きく
// （まるでスケールが変わったように）判定されてしまっていたため、こちらに置き換えている。
struct OBB {
    Vector3 center{ 0.0f, 0.0f, 0.0f };            // ワールド空間での中心
    Vector3 axis[3]{ {1,0,0}, {0,1,0}, {0,0,1} };  // ボックスのX/Y/Z軸方向（正規化済み、ワールド回転を反映）
    Vector3 halfSize{ 0.5f, 0.5f, 0.5f };          // 各軸方向の半径（中心から面までの距離。ワールドスケール反映済み）
};

// 頂点位置の配列（モデルのローカル座標系）から、ローカル空間のAABBを計算する
// モデル読み込み時（GPUバッファ作成時など）に1回だけ呼べばよい
AABB ComputeLocalAABB(const std::vector<Vector3>& positions);

// ローカル空間のAABBを、ワールド行列（回転・スケール・平行移動）を反映したOBBに変換する
// Unity同様、オブジェクトを回転させるとボックスコライダーもそのまま一緒に回転する
OBB TransformToOBB(const AABB& localAABB, const Matrix4x4& worldMatrix);

// OBBを描画するためのワールド行列を作る（0〜1のユニットボックスをOBBの形状・向きに変形する）
Matrix4x4 MakeOBBWorldMatrix(const OBB& obb);

// 2つのOBBが重なっているかどうかを分離軸判定（SAT: Separating Axis Theorem）で調べる
bool Intersect(const OBB& a, const OBB& b);

// 2つのOBBが重なっている場合、aをbから押し出すための最小移動ベクトル（Minimum Translation Vector）を求める
// 戻り値はtrue=重なっている（このときのみmtvに値を書き込む）。
// aのtranslateにmtvをそのまま加算すると、めり込みがちょうど解消される
bool ComputeMTV(const OBB& a, const OBB& b, Vector3& mtv);

// ===== 分離軸判定の詳細な結果 =====
// 「重なっているか」だけでなく「どういう当たり方をしているか」まで返す。
// 接触点を作るときに、当たり方ごとに求め方がまったく違うため必要になる。
//
//   FaceA / FaceB : どちらかの面が接触面。相手の頂点が内部にめり込んでいるので、
//                   頂点を拾えば接触点が求まる（面で接していれば4点、辺なら2点、角なら1点）
//   EdgeEdge      : 辺どうしが斜めに交差している。このときはどちらの頂点も相手の内部に入っていないため、
//                   頂点を探しても1つも見つからない。交差している辺の組を直接求めて、
//                   2本の線分の最接近点を接触点にする必要がある
enum class SATFeature { FaceA, FaceB, EdgeEdge };

struct SATResult {
    Vector3 axis{ 0.0f, 1.0f, 0.0f }; // 最小めり込みの分離軸（正規化済み、aからbへ向く向きに揃えてある）
    float penetration = 0.0f;         // その軸方向のめり込み量
    SATFeature feature = SATFeature::FaceA;
    int indexA = 0; // FaceA:aのどの面か / EdgeEdge:aのどの軸方向の辺か（0=X,1=Y,2=Z）
    int indexB = 0; // FaceB / EdgeEdge における同様の番号

    // ===== 辺どうしの軸だけを別に取っておいたもの =====
    // 上のfeatureは、接している物がガタつかないよう面をわずかに優遇して決めている。
    // そのため本当は辺で交差しているのに面と分類されることがあり、
    // 「頂点がどこにも入っていない＝辺の交差しかありえない」と分かっている場面では
    // 優遇なしのこちらを使う必要がある
    bool hasEdgeAxis = false;
    Vector3 edgeAxis{ 0.0f, 1.0f, 0.0f }; // 同じくaからbへ向く向き
    float edgePenetration = 0.0f;
    int edgeIndexA = 0;
    int edgeIndexB = 0;
};

// 15軸すべてで分離軸判定を行い、最も浅いめり込みの軸と、その当たり方を返す
bool ComputeSAT(const OBB& a, const OBB& b, SATResult& out);

// 2本の線分の最接近点を求める（辺どうしの接触点を出すのに使う）
void ClosestPointsBetweenSegments(const Vector3& p1, const Vector3& q1,
                                  const Vector3& p2, const Vector3& q2,
                                  Vector3& outOn1, Vector3& outOn2);

// 実際に触れている2本の辺を特定し、その最接近点を返す（sat.edgeAxis側の情報を使う）。
// 呼び出す前に sat.hasEdgeAxis を確かめること
void FindContactEdgePoints(const OBB& a, const OBB& b, const SATResult& sat,
                           Vector3& outPointOnA, Vector3& outPointOnB);

// ===== 接触点の一覧（コンタクトマニフォールド）を作る =====
// 2つの箱がどこで触れているかを、最大8点まで求める。
//
// 「相手の内部に入っている頂点を拾う」という素朴な方法では足りない。
// 角材を十字に重ねた形（＋の字）では、どちらの箱の頂点も相手の内部に入らないのに
// しっかり重なっている。頂点だけを見ていると、この当たり方をまるごと取りこぼして
// すり抜けたり沈み込んだりする。
//
// そこで、分離軸判定で分かった「どの面／辺で当たっているか」に応じて求め方を変える：
//   面が接触面 … 相手の面を、こちらの面の縁で切り取る（クリッピング）。
//                切り取って残った角が接触点になる。面どうしなら4点、辺なら2点、角なら1点と、
//                実際の支え方どおりの数が自然に出る
//   辺どうし   … 交差している2本の辺の最接近点1点。辺の接触は本質的に1点なのでこれで足りる
//
// outNormalは「aをbから引き離す向き」の単位ベクトル。戻り値は求まった接触点の数
constexpr int kMaxManifoldPoints = 8;
int BuildContactManifold(const OBB& a, const OBB& b, const SATResult& sat,
                         Vector3 outPoints[kMaxManifoldPoints],
                         float outDepths[kMaxManifoldPoints],
                         Vector3& outNormal);

// 球とOBBが重なっているかどうかの簡易判定（メッシュコライダーの「広域判定（ブロードフェーズ）」用）
// 三角形単位の精密なメッシュ判定は重いため、まずこの軽量なOBB判定で「当たる可能性があるか」だけを絞り込み、
// trueのときだけ精密なメッシュ判定に進むことで負荷を軽減する
bool IntersectSphereOBB(const Vector3& center, float radius, const OBB& obb);

// レイ（origin, dir）とOBBの交差判定（スラブ法）。マウスクリックによるオブジェクト選択（ピッキング）に使う
// 戻り値はtrue=交差（このときのみoutDistanceにレイ原点からの距離を書き込む）
bool RayIntersectOBB(const Vector3& rayOrigin, const Vector3& rayDir, const OBB& obb, float& outDistance);
