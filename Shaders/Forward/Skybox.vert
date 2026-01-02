// Copyright Monster Engine. All Rights Reserved.
// Skybox Vertex Shader
// 
// Renders a skybox cubemap at infinite distance.
// Uses view rotation only (no translation) so skybox appears infinitely far.
//
// Reference: UE5 SkyPassVertexShader.usf

#version 450

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;    // Unit cube vertex position

// ============================================================================
// Uniform Buffers
// ============================================================================

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

// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec3 outTexCoord;  // Direction for cubemap sampling

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Use vertex position as cubemap texture coordinate
    // This works because we're rendering a unit cube centered at origin
    outTexCoord = inPosition;
    
    // Create view matrix without translation (rotation only)
    // This makes the skybox appear at infinite distance
    mat4 viewRotation = mat4(mat3(View.ViewMatrix));
    
    // UE5 row-vector convention: v * M (position on left, matrix on right)
    vec4 clipPos = vec4(inPosition, 1.0) * viewRotation * View.ProjectionMatrix;
    
    // Set z = w so depth is always 1.0 (at far plane)
    // This ensures skybox is rendered behind all other geometry
    gl_Position = clipPos.xyww;
}
