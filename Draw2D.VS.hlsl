// 2Dの絵と図形を描く頂点シェーダー。
//
// 図形（四角・円）と絵（イラスト）を同じシェーダーで描く。
// 分けると、図形と絵を交互に描いたときに重なり順が保てない
//（パイプラインを切り替えるたびに描き直しの順番が崩れる）。
// 図形のときは白1x1のテクスチャを貼るので、色だけがそのまま出る。
struct TransformationMatrix {
    float4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransform : register(b0);

struct VertexShaderInput {
    float3 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(float4(input.position, 1.0f), gTransform.WVP);
    output.texcoord = input.texcoord;
    return output;
}
