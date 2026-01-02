// Copyright Monster Engine. All Rights Reserved.
// Shadow Depth Vertex Shader
// 
// Renders scene depth from light's perspective for shadow mapping.
// Supports directional, point, and spot lights.
//
// Reference: UE5 ShadowDepthVertexShader.usf

#version 450

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;    // Object-space position
layout(location = 1) in vec2 inTexCoord;    // Texture coordinates (for alpha test)

// ============================================================================
// Uniform Buffers
// ============================================================================

// Shadow pass uniform buffer
layout(set = 0, binding = 0) uniform ShadowPassData
{
    mat4 LightViewMatrix;           // World to light view
    mat4 LightProjectionMatrix;     // Light view to clip
    mat4 LightViewProjectionMatrix; // Combined light VP
    vec4 LightPosition;             // Light position (w = 1 for point/spot, w = 0 for directional)
    vec4 LightDirection;            // Light direction (for directional/spot)
    float DepthBias;                // Constant depth bias
    float SlopeScaledBias;          // Slope-scaled depth bias
    float NormalOffsetBias;         // Normal offset bias
    float ShadowDistance;           // Maximum shadow distance
} ShadowPass;

// Object uniform buffer
layout(set = 0, binding = 1) uniform ObjectData
{
    mat4 WorldMatrix;
    mat4 InvWorldMatrix;
    mat4 WorldMatrixIT;
    vec4 ObjectBoundsCenter;
    vec4 ObjectBoundsExtent;
} Object;

// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec2 outTexCoord;      // For alpha test
layout(location = 1) out float outDepth;        // Linear depth for point lights

// ============================================================================
// Main
// ============================================================================

void main()
{
    // UE5 row-vector convention: v * M (position on left, matrix on right)
    // Transform to world space
    vec4 worldPosition = vec4(inPosition, 1.0) * Object.WorldMatrix;
    
    // Transform to light clip space
    vec4 clipPosition = worldPosition * ShadowPass.LightViewProjectionMatrix;
    
    // Apply depth bias
    // Note: Hardware depth bias is preferred, but this provides additional control
    clipPosition.z += ShadowPass.DepthBias;
    
    gl_Position = clipPosition;
    
    // Pass texture coordinates for alpha test
    outTexCoord = inTexCoord;
    
    // Calculate linear depth for point light shadow maps
    // This is needed because cube shadow maps use linear depth
    if (ShadowPass.LightPosition.w > 0.5) // Point or spot light
    {
        vec3 toLight = worldPosition.xyz - ShadowPass.LightPosition.xyz;
        outDepth = length(toLight) / ShadowPass.ShadowDistance;
    }
    else
    {
        outDepth = clipPosition.z / clipPosition.w;
    }
}
