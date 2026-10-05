#include "Particle.hlsli"

// 粒の情報の配列。ConstantBufferではなくStructuredBuffer（SRV）なので、レジスタはtを使う
StructuredBuffer<ParticleInstance> gParticles : register(t0);

// firstInstance : この描画の先頭が、配列のどこから始まるか。
//   SV_InstanceIDは、DrawInstancedの開始位置を足さずに常に0から数える。
//   エミッターごとに描き分けるので、自分で先頭の位置を足して引く
// soft          : ピクセルシェーダーが使う（丸くぼかすか）
cbuffer gDraw : register(b0)
{
    uint gFirstInstance;
    uint gSoft;
}

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 使わない（Object3Dと頂点の形を揃えてある）
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    ParticleInstance p = gParticles[gFirstInstance + instanceId];

    VertexShaderOutput output;
    output.position = mul(input.position, p.WVP);
    output.texcoord = input.texcoord;
    output.color = p.color;
    return output;
}
