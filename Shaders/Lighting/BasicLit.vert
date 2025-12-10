#version 450
// Copyright Monster Engine. All Rights Reserved.

/**
 * @file BasicLit.vert
 * @brief Basic lit vertex shader
 * 
 * Transforms vertices and passes data to fragment shader for lighting.
 * Supports multiple lights with Blinn-Phong and PBR lighting models.
 */

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View/Projection matrices
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    vec3 CameraPosition;
    float Padding0;
} View;

// Model transform (per-object)
layout(set = 1, binding = 0) uniform ObjectData
{
    mat4 ModelMatrix;
    mat4 NormalMatrix;  // Inverse transpose of model matrix (upper 3x3)
} Object;

// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragViewDir;

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Transform position to world space
    vec4 worldPos = Object.ModelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (using normal matrix for non-uniform scaling)
    fragNormal = normalize(mat3(Object.NormalMatrix) * inNormal);
    
    // Transform tangent to world space
    fragTangent = normalize(mat3(Object.NormalMatrix) * inTangent);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Calculate view direction (from surface to camera)
    fragViewDir = normalize(View.CameraPosition - fragWorldPos);
    
    // Transform to clip space
    gl_Position = View.ViewProjectionMatrix * worldPos;
}
