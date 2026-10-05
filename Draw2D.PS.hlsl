// 2Dの絵と図形を描くピクセルシェーダー。
//
// テクスチャの色に、指定された色を掛ける。
//   図形 … 白1x1を貼るので、指定した色がそのまま出る
//   絵   … 白を指定すれば元の絵のまま、色を薄くすれば透ける、色を掛ければ染まる
// 掛け算にしてあるので、絵を暗くしたり点滅させたりが色の指定だけでできる。
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer DrawColor : register(b1) {
    float4 gColor;
};

struct PixelShaderInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input) {
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord) * gColor;
    // 完全に透明な点は書かない。半透明を重ねたときに、
    // 透明な部分が背景を薄く濁らせるのを防ぐ
    if (output.color.a <= 0.001f) { discard; }
    return output;
}
