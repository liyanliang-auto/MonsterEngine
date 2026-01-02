/**
 * Default Material Vertex Shader
 * 
 * Standard vertex shader for opaque materials with:
 * - Position transformation
 * - Normal transformation
 * - Texture coordinate pass-through
 */

#version 450

// ============================================================================
// Vertex Inputs
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;  // xyz = tangent, w = handedness

// ============================================================================
// Vertex Outputs
// ============================================================================

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniforms (set 0, binding 0)
layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 cameraPosition;    // xyz = position, w = unused
    vec4 cameraDirection;   // xyz = direction, w = unused
} view;

// Object uniforms (set 1, binding 0)
layout(set = 1, binding = 0) uniform ObjectUniforms {
    mat4 modelMatrix;
    mat4 normalMatrix;      // Inverse transpose of model matrix (upper 3x3)
} object;

// ============================================================================
// Main
// ============================================================================

void main()
{
    // UE5 row-vector convention: v * M (position on left, matrix on right)
    // Transform position to world space
    vec4 worldPos = vec4(inPosition, 1.0) * object.modelMatrix;
    fragWorldPos = worldPos.xyz;
    
    // Transform position to clip space
    gl_Position = worldPos * view.viewProjMatrix;
    
    // Transform normal to world space
    fragNormal = normalize(inNormal * mat3(object.normalMatrix));
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Transform tangent to world space
    fragTangent = normalize(inTangent.xyz * mat3(object.modelMatrix));
    
    // Calculate bitangent
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;
}
