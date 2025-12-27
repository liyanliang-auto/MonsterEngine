// Copyright Monster Engine. All Rights Reserved.
// 
// StandardPBR.vert - Standard PBR Vertex Shader (Vulkan)
// 
// This shader transforms vertices and prepares data for PBR fragment shading.
// Reference: UE5 BasePassVertexShader.usf
//
// Outputs:
// - World position for lighting calculations
// - World normal, tangent, bitangent for normal mapping
// - Texture coordinates
// - View direction for specular calculations

#version 450

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

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outWorldTangent;
layout(location = 4) out vec3 outWorldBitangent;
layout(location = 5) out vec3 outViewDirection;
layout(location = 6) out vec4 outClipPosition;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer (set 0, binding 0)
layout(set = 0, binding = 0) uniform ViewData
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

// Object uniform buffer (set 0, binding 1)
layout(set = 0, binding = 1) uniform ObjectData
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
    outWorldPosition = worldPosition.xyz;
    
    // Transform normal to world space (using normal matrix for correct scaling)
    outWorldNormal = normalize(mat3(Object.NormalMatrix) * inNormal);
    
    // Transform tangent and bitangent to world space
    outWorldTangent = normalize(mat3(Object.ModelMatrix) * inTangent);
    outWorldBitangent = normalize(mat3(Object.ModelMatrix) * inBitangent);
    
    // Ensure TBN is orthonormal (Gram-Schmidt orthogonalization)
    outWorldTangent = normalize(outWorldTangent - dot(outWorldTangent, outWorldNormal) * outWorldNormal);
    outWorldBitangent = cross(outWorldNormal, outWorldTangent);
    
    // Pass through texture coordinates
    outTexCoord = inTexCoord;
    
    // Calculate view direction (from surface to camera)
    outViewDirection = View.ViewOrigin.xyz - worldPosition.xyz;
    
    // Transform to clip space
    outClipPosition = View.ViewProjectionMatrix * worldPosition;
    gl_Position = outClipPosition;
}
