// Copyright Monster Engine. All Rights Reserved.
// Shadow Depth Fragment Shader
// 
// Outputs depth for shadow mapping.
// Supports alpha test for masked materials.
// For point lights, outputs linear depth to color attachment.
//
// Reference: UE5 ShadowDepthPixelShader.usf

#version 450

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in float inDepth;

// ============================================================================
// Fragment Output
// ============================================================================

// For point light shadow maps, we output linear depth to a color attachment
// For directional/spot lights, we only use the depth buffer
layout(location = 0) out float outDepth;

// ============================================================================
// Uniforms
// ============================================================================

// Shadow pass data
layout(set = 0, binding = 0) uniform ShadowPassData
{
    mat4 LightViewMatrix;
    mat4 LightProjectionMatrix;
    mat4 LightViewProjectionMatrix;
    vec4 LightPosition;
    vec4 LightDirection;
    float DepthBias;
    float SlopeScaledBias;
    float NormalOffsetBias;
    float ShadowDistance;
} ShadowPass;

// Material parameters for alpha test
layout(set = 0, binding = 3) uniform MaterialData
{
    vec4 BaseColor;
    vec4 EmissiveColor;
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
    float NormalScale;
    float AlphaCutoff;
    float Reflectance;
    float ClearCoat;
    float ClearCoatRoughness;
} Material;

// Push constant for alpha test and point light mode
layout(push_constant) uniform PushConstants
{
    int bAlphaTest;         // 0 = no alpha test, 1 = alpha test enabled
    int bPointLight;        // 0 = directional/spot, 1 = point light (output linear depth)
} Constants;

// ============================================================================
// Textures
// ============================================================================

layout(set = 1, binding = 0) uniform sampler2D BaseColorMap;

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Alpha test for masked materials
    if (Constants.bAlphaTest != 0)
    {
        float alpha = texture(BaseColorMap, inTexCoord).a * Material.BaseColor.a;
        if (alpha < Material.AlphaCutoff)
        {
            discard;
        }
    }
    
    // For point lights, output linear depth to color attachment
    // This is needed for cube shadow maps
    if (Constants.bPointLight != 0)
    {
        outDepth = inDepth;
    }
    else
    {
        // For directional/spot lights, depth buffer is sufficient
        // Output something to avoid undefined behavior
        outDepth = gl_FragCoord.z;
    }
}
