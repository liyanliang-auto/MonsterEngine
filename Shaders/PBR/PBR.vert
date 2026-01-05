//------------------------------------------------------------------------------
// PBR.vert - PBR Vertex Shader (Vulkan GLSL)
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BasePassVertexShader
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#version 450

//------------------------------------------------------------------------------
// Descriptor Set 0: Per-Frame Data
//------------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform ViewUniformBuffer {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    mat4 InvViewProjectionMatrix;
    vec4 CameraPosition;      // xyz = position, w = unused
    vec4 CameraDirection;     // xyz = forward, w = unused
    vec4 ViewportSize;        // xy = size, zw = 1/size
    vec4 TimeParams;          // x = time, y = sin(time), z = cos(time), w = deltaTime
} View;

//------------------------------------------------------------------------------
// Descriptor Set 2: Per-Object Data
//------------------------------------------------------------------------------

layout(set = 2, binding = 0) uniform ObjectUniformBuffer {
    mat4 ModelMatrix;
    mat4 NormalMatrix;        // Inverse transpose of model matrix (upper 3x3)
    vec4 ObjectBoundsMin;
    vec4 ObjectBoundsMax;
} Object;

//------------------------------------------------------------------------------
// Vertex Inputs
//------------------------------------------------------------------------------

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;   // xyz = tangent, w = bitangent sign
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inColor;

//------------------------------------------------------------------------------
// Vertex Outputs
//------------------------------------------------------------------------------

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec4 outWorldTangent;    // xyz = tangent, w = bitangent sign
layout(location = 3) out vec2 outTexCoord0;
layout(location = 4) out vec2 outTexCoord1;
layout(location = 5) out vec4 outVertexColor;
layout(location = 6) out vec3 outViewDirection;   // Direction from surface to camera

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

void main() {
    // Transform position to world space (row vector * matrix for row-major)
    vec4 worldPosition = vec4(inPosition, 1.0) * Object.ModelMatrix;
    outWorldPosition = worldPosition.xyz;
    
    // Transform normal to world space using normal matrix
    // Normal matrix is the inverse transpose of the model matrix
    vec3 worldNormal = normalize((vec4(inNormal, 0.0) * Object.NormalMatrix).xyz);
    outWorldNormal = worldNormal;
    
    // Transform tangent to world space
    vec3 worldTangent = normalize((vec4(inTangent.xyz, 0.0) * Object.ModelMatrix).xyz);
    outWorldTangent = vec4(worldTangent, inTangent.w);
    
    // Pass through texture coordinates
    outTexCoord0 = inTexCoord0;
    outTexCoord1 = inTexCoord1;
    
    // Pass through vertex color
    outVertexColor = inColor;
    
    // Compute view direction (from surface to camera)
    outViewDirection = normalize(View.CameraPosition.xyz - worldPosition.xyz);
    
    // Transform to clip space (row vector * matrix for row-major)
    gl_Position = worldPosition * View.ViewProjectionMatrix;
}
