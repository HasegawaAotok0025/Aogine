#include "object3d.hlsli"

struct Material
{
    float4 color;
    int lightingMode;  // 0:Lightingなし 1:Lambert 2:HalfLambert（旧enableLightingを3値化したもの）
    float3 padding;    // 16バイト境界合わせ用パディング
    float4x4 uvTransform; // UV座標変換行列（Translate/Rotate/Scale合成済み）
};

cbuffer gMaterial : register(b0)
{
    Material gMaterial;
}

struct DirectionalLight
{
    float4 color;     //!< ライトの色
    float3 direction; //!< ライトの向き
    float intensity;  //!< 輝度
};

cbuffer gDirectionalLight : register(b1)
{
    DirectionalLight gDirectionalLight;
}

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // UVTransformを適用してからテクスチャをサンプリングする
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    float4 outputColor;
    if (gMaterial.lightingMode == 1)
    {
        // Lambert（拡散反射をそのまま利用する。負の値は0にクランプ）
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cosVal = saturate(NdotL);
        outputColor = gMaterial.color * textureColor * gDirectionalLight.color * cosVal * gDirectionalLight.intensity;
    }
    else if (gMaterial.lightingMode == 2)
    {
        // Half Lambert
        // 入力法線は補間により長さが崩れているため再度正規化する
        // NdotL: 法線とライト方向（ライト側への向き）の内積
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        // 内積の範囲[-1,1]を[0,1]へなだらかに変形し、さらに2乗してそれっぽさを強調する
        float cosVal = pow(NdotL * 0.5f + 0.5f, 2.0f);
        outputColor = gMaterial.color * textureColor * gDirectionalLight.color * cosVal * gDirectionalLight.intensity;
    }
    else
    {
        // Lightingしない場合。従来通りの演算
        outputColor = gMaterial.color * textureColor;
    }

    // αはライトの計算に巻き込まず、マテリアルの色とテクスチャのαだけで決める
    // （Lambertなどで暗くなった分だけ透けてしまうのを防ぐ）
    outputColor.a = gMaterial.color.a * textureColor.a;

    return outputColor;
}
