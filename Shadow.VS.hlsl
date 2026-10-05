// 影（地面に落とす丸い染み）用の頂点シェーダー。
// 頂点はXZ平面に置いた1辺1の四角形（-0.5〜0.5）で、位置と大きさはワールド行列側で決める。
// ローカル位置をそのままピクセルシェーダーへ渡し、中心からの距離で丸く濃淡をつける
struct TransformationMatrix {
    float4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransform : register(b0);

struct VertexShaderInput {
    float3 position : POSITION0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0; // -0.5〜0.5。中心が(0,0)
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(float4(input.position, 1.0f), gTransform.WVP);
    output.local = input.position.xz;
    return output;
}
