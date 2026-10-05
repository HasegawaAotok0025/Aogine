// 文字描画用のピクセルシェーダー。
// フォントの図形は「そこに文字があるか」の濃さだけを1チャンネル(R8)で持っている。
// 色は頂点から来るので、濃さをそのまま不透明度に使って重ねる（半透明合成はPSO側で有効にしてある）
Texture2D<float> gGlyphAtlas : register(t0);
SamplerState gSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float coverage = gGlyphAtlas.Sample(gSampler, input.texcoord);
    return float4(input.color.rgb, input.color.a * coverage);
}
