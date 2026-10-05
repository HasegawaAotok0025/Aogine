// 文字描画用の頂点シェーダー。
// 頂点はピクセル単位の画面座標（左上が原点、右下がclientWidth/Height）でそのまま渡ってくるので、
// ここで描画装置が使う正規化座標（-1〜1、Yが上向き）へ直すだけでよい。
// 画面サイズを掛け算1回で済ませられるよう、逆数（1/幅、1/高さ）の形で受け取る
cbuffer ScreenParams : register(b0)
{
    float2 gInvScreenSize;
};

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float2 ndc = input.position * gInvScreenSize * 2.0f - 1.0f;
    ndc.y = -ndc.y; // 画面のYは下向き、正規化座標のYは上向きなので反転する
    output.position = float4(ndc, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
