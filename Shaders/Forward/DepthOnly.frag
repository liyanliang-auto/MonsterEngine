// Copyright Monster Engine. All Rights Reserved.
// Depth-Only Fragment Shader
// 
// Minimal fragment shader for depth prepass.
// Optionally performs alpha test for masked materials.
//
// Reference: UE5 DepthOnlyPixelShader.usf

#version 450

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec2 inTexCoord;

// ============================================================================
// Uniforms
// ============================================================================

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

// Push constant for enabling/disabling alpha test
layout(push_constant) uniform PushConstants
{
    int bAlphaTest;     // 0 = no alpha test, 1 = alpha test enabled
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
    
    // No color output - depth is written automatically
    // gl_FragDepth is set automatically from gl_Position.z
}
