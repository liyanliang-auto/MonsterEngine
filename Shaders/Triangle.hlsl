// Simple Triangle Vertex and Pixel Shaders
// This demonstrates basic shader functionality for the triangle demo

// Vertex Shader
struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

// Pixel Shader
float4 PSMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0f);
}
