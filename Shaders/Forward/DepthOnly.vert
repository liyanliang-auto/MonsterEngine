// Copyright Monster Engine. All Rights Reserved.
// Depth-Only Vertex Shader
// 
// Minimal vertex shader for depth prepass and shadow map rendering.
// Only transforms position to clip space, no lighting calculations.
//
// Reference: UE5 DepthOnlyVertexShader.usf

#version 450

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;    // Object-space position
layout(location = 1) in vec2 inTexCoord;    // Texture coordinates (for alpha test)

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    vec4 ViewOrigin;
    vec4 ViewForward;
    vec4 ViewRight;
    vec4 ViewUp;
    vec4 ScreenParams;
    vec4 ZBufferParams;
    float Time;
    float DeltaTime;
    float Padding0;
    float Padding1;
} View;

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

layout(location = 0) out vec2 outTexCoord;  // For alpha test in fragment shader

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Transform to world space
    vec4 worldPosition = Object.WorldMatrix * vec4(inPosition, 1.0);
    
    // Transform to clip space
    gl_Position = View.ViewProjectionMatrix * worldPosition;
    
    // Pass texture coordinates for potential alpha test
    outTexCoord = inTexCoord;
}
