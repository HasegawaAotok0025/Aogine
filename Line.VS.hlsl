struct TransformationMatrix {
    float4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransform : register(b0);

struct VertexShaderInput {
    float3 position : POSITION0;
};

struct VertexShaderOutput {
    float4 position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(float4(input.position, 1.0f), gTransform.WVP);
    return output;
}
