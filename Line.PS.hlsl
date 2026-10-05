struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

cbuffer LineColor : register(b1) {
    float4 gColor; // 衝突していなければ緑、衝突していれば赤（C++側からRoot Constantで渡す）
};

PixelShaderOutput main() {
    PixelShaderOutput output;
    output.color = gColor;
    return output;
}
