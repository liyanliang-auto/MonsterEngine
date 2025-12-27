// Copyright Monster Engine. All Rights Reserved.
// 
// StandardPBR_GL.vert - Standard PBR Vertex Shader (OpenGL)
// 
// This shader transforms vertices and prepares data for PBR fragment shading.
// Reference: UE5 BasePassVertexShader.usf
//
// OpenGL version differences from Vulkan:
// - Uses #version 330 core for wider compatibility
// - Uses uniform blocks without explicit set/binding (uses binding only)
// - Y-axis is not flipped in clip space

#version 330 core

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

// ============================================================================
// Vertex Output
// ============================================================================

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec3 vWorldTangent;
out vec3 vWorldBitangent;
out vec3 vViewDirection;
out vec4 vClipPosition;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer
layout(std140) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    vec4 ViewOrigin;          // xyz = camera position, w = unused
    vec4 ViewForward;         // xyz = camera forward, w = unused
    vec4 ViewRight;           // xyz = camera right, w = unused
    vec4 ViewUp;              // xyz = camera up, w = unused
    vec4 ScreenParams;        // x = width, y = height, z = 1/width, w = 1/height
    vec4 ZBufferParams;       // x = 1-far/near, y = far/near, z = x/far, w = y/far
    float Time;
    float DeltaTime;
    float Padding0;
    float Padding1;
} View;

// Object uniform buffer
layout(std140) uniform ObjectData
{
    mat4 ModelMatrix;
    mat4 NormalMatrix;        // Inverse transpose of model matrix (for normals)
    mat4 PrevModelMatrix;     // Previous frame model matrix (for motion vectors)
    vec4 ObjectBounds;        // xyz = center, w = radius
} Object;

// ============================================================================
// Main Vertex Shader
// ============================================================================

void main()
{
    // Transform position to world space
    vec4 worldPosition = Object.ModelMatrix * vec4(inPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    
    // Transform normal to world space (using normal matrix for correct scaling)
    vWorldNormal = normalize(mat3(Object.NormalMatrix) * inNormal);
    
    // Transform tangent and bitangent to world space
    vWorldTangent = normalize(mat3(Object.ModelMatrix) * inTangent);
    vWorldBitangent = normalize(mat3(Object.ModelMatrix) * inBitangent);
    
    // Ensure TBN is orthonormal (Gram-Schmidt orthogonalization)
    vWorldTangent = normalize(vWorldTangent - dot(vWorldTangent, vWorldNormal) * vWorldNormal);
    vWorldBitangent = cross(vWorldNormal, vWorldTangent);
    
    // Pass through texture coordinates
    vTexCoord = inTexCoord;
    
    // Calculate view direction (from surface to camera)
    vViewDirection = View.ViewOrigin.xyz - worldPosition.xyz;
    
    // Transform to clip space
    vClipPosition = View.ViewProjectionMatrix * worldPosition;
    gl_Position = vClipPosition;
}
