#include "object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

cbuffer gTransformationMatrix : register(b0)
{
    TransformationMatrix gTransformationMatrix;
}

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    // 法線をワールド座標系に変換する（平行移動は不要なので3x3を利用し、拡縮回転のみ適用）
    output.normal = normalize(mul(input.normal, (float3x3)gTransformationMatrix.World));
    return output;
}