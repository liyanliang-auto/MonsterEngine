// Copyright Monster Engine. All Rights Reserved.
// Forward Lighting Vertex Shader
// 
// This shader transforms vertices and prepares data for forward lighting calculations.
// Outputs world-space position, normal, tangent, and view direction for the fragment shader.
//
// Reference: UE5 BasePassVertexShader.usf

#version 450

// ============================================================================
// Vertex Input Attributes
// ============================================================================

layout(location = 0) in vec3 inPosition;    // Object-space position
layout(location = 1) in vec3 inNormal;      // Object-space normal
layout(location = 2) in vec2 inTexCoord;    // Texture coordinates
layout(location = 3) in vec3 inTangent;     // Object-space tangent
layout(location = 4) in vec3 inBitangent;   // Object-space bitangent (optional)

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer (binding 0, set 0)
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;            // World to view transform
    mat4 ProjectionMatrix;      // View to clip transform
    mat4 ViewProjectionMatrix;  // Combined view-projection
    mat4 InvViewMatrix;         // View to world transform
    mat4 InvProjectionMatrix;   // Clip to view transform
    vec4 ViewOrigin;            // Camera position in world space (w = 1)
    vec4 ViewForward;           // Camera forward direction (w = 0)
    vec4 ViewRight;             // Camera right direction (w = 0)
    vec4 ViewUp;                // Camera up direction (w = 0)
    vec4 ScreenParams;          // x = width, y = height, z = 1/width, w = 1/height
    vec4 ZBufferParams;         // x = near, y = far, z = 1/near, w = 1/far
    float Time;                 // Total elapsed time
    float DeltaTime;            // Time since last frame
    float Padding0;
    float Padding1;
} View;

// Object uniform buffer (binding 1, set 0)
layout(set = 0, binding = 1) uniform ObjectData
{
    mat4 WorldMatrix;           // Object to world transform
    mat4 InvWorldMatrix;        // World to object transform
    mat4 WorldMatrixIT;         // Inverse transpose for normal transform
    vec4 ObjectBoundsCenter;    // Object bounds center in world space
    vec4 ObjectBoundsExtent;    // Object bounds half-extents
} Object;

// ============================================================================
// Vertex Output (to Fragment Shader)
// ============================================================================

layout(location = 0) out vec3 outWorldPosition;     // World-space position
layout(location = 1) out vec3 outWorldNormal;       // World-space normal
layout(location = 2) out vec2 outTexCoord;          // Texture coordinates
layout(location = 3) out vec3 outWorldTangent;      // World-space tangent
layout(location = 4) out vec3 outWorldBitangent;    // World-space bitangent
layout(location = 5) out vec3 outViewDirection;     // Direction from surface to camera
layout(location = 6) out vec4 outClipPosition;      // Clip-space position (for shadow mapping)

// ============================================================================
// Main Vertex Shader
// ============================================================================

void main()
{
    // UE5 row-vector convention: v * M (position on left, matrix on right)
    // Transform position to world space
    vec4 worldPosition = vec4(inPosition, 1.0) * Object.WorldMatrix;
    outWorldPosition = worldPosition.xyz;
    
    // Transform normal to world space (using inverse transpose)
    // This correctly handles non-uniform scaling
    outWorldNormal = normalize(inNormal * mat3(Object.WorldMatrixIT));
    
    // Transform tangent to world space
    outWorldTangent = normalize(inTangent * mat3(Object.WorldMatrix));
    
    // Calculate bitangent
    // If bitangent is provided, transform it; otherwise, compute from normal and tangent
    if (length(inBitangent) > 0.001)
    {
        outWorldBitangent = normalize(inBitangent * mat3(Object.WorldMatrix));
    }
    else
    {
        // Compute bitangent from cross product
        outWorldBitangent = cross(outWorldNormal, outWorldTangent);
    }
    
    // Re-orthogonalize TBN (Gram-Schmidt process)
    outWorldTangent = normalize(outWorldTangent - dot(outWorldTangent, outWorldNormal) * outWorldNormal);
    outWorldBitangent = cross(outWorldNormal, outWorldTangent);
    
    // Pass through texture coordinates
    outTexCoord = inTexCoord;
    
    // Calculate view direction (from surface to camera)
    outViewDirection = normalize(View.ViewOrigin.xyz - outWorldPosition);
    
    // Transform to clip space (row-vector: v * VP)
    vec4 clipPosition = worldPosition * View.ViewProjectionMatrix;
    outClipPosition = clipPosition;
    
    // Output final clip-space position
    gl_Position = clipPosition;
}
