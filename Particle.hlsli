// Particle用の、頂点シェーダー → ピクセルシェーダーの受け渡し
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0; // 粒ごとの色（時間で変わる）
};

// 粒1つぶんの情報。C++側の ParticleInstance と並びを合わせること
struct ParticleInstance
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
};
