// 影用のピクセルシェーダー。
// テクスチャを使わず、四角形の中心からの距離だけで丸い形と、ふちのぼかしを作る。
// こうしておくと画像ファイルが要らず、どんな大きさに拡大しても輪郭がぼやけない
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
};

// 色と濃さ。gColor.aが影の一番濃いところの不透明度になる
cbuffer ShadowColor : register(b1) {
    float4 gColor;
};

float4 main(VertexShaderOutput input) : SV_TARGET {
    // 中心で0、四角形の内接円の縁で1になる距離
    float distanceFromCenter = length(input.local) * 2.0f;

    // 1 - 距離 を2乗して、真ん中が濃く外へ向かってなだらかに薄くなるようにする。
    // そのまま線形に薄くすると輪郭が硬く見え、地面に貼った円盤のようになってしまう
    float falloff = saturate(1.0f - distanceFromCenter);
    falloff = falloff * falloff;

    return float4(gColor.rgb, gColor.a * falloff);
}
