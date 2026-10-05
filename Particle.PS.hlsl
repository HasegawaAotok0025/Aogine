#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer gDraw : register(b0)
{
    uint gFirstInstance; // 頂点シェーダーが使う
    uint gSoft;          // 1なら、絵に関わらず中心から縁へ薄くなる丸にする
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.texcoord) * input.color;

    if (gSoft != 0)
    {
        // 板の中心からの距離（縁で1）。なめらかに薄くして、四角い縁を消す
        float d = length(input.texcoord - 0.5f) * 2.0f;
        float mask = saturate(1.0f - d);
        color.a *= mask * mask;
    }

    // 完全に透明な点は書かない。透明な部分が背景を濁らせるのを防ぐ
    if (color.a <= 0.001f)
    {
        discard;
    }
    return color;
}
