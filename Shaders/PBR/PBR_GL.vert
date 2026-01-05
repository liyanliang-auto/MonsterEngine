//------------------------------------------------------------------------------
// PBR_GL.vert - PBR Vertex Shader (OpenGL GLSL)
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BasePassVertexShader
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#version 330 core

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------

// View uniforms
uniform mat4 u_ViewMatrix;
uniform mat4 u_ProjectionMatrix;
uniform mat4 u_ViewProjectionMatrix;
uniform vec3 u_CameraPosition;

// Object uniforms
uniform mat4 u_ModelMatrix;
uniform mat4 u_NormalMatrix;

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

out vec3 v_WorldPosition;
out vec3 v_WorldNormal;
out vec4 v_WorldTangent;
out vec2 v_TexCoord0;
out vec2 v_TexCoord1;
out vec4 v_VertexColor;
out vec3 v_ViewDirection;

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

void main() {
    // Transform position to world space (row vector * matrix for row-major)
    vec4 worldPosition = vec4(inPosition, 1.0) * u_ModelMatrix;
    v_WorldPosition = worldPosition.xyz;
    
    // Transform normal to world space using normal matrix
    vec3 worldNormal = normalize((vec4(inNormal, 0.0) * u_NormalMatrix).xyz);
    v_WorldNormal = worldNormal;
    
    // Transform tangent to world space
    vec3 worldTangent = normalize((vec4(inTangent.xyz, 0.0) * u_ModelMatrix).xyz);
    v_WorldTangent = vec4(worldTangent, inTangent.w);
    
    // Pass through texture coordinates
    v_TexCoord0 = inTexCoord0;
    v_TexCoord1 = inTexCoord1;
    
    // Pass through vertex color
    v_VertexColor = inColor;
    
    // Compute view direction (from surface to camera)
    v_ViewDirection = normalize(u_CameraPosition - worldPosition.xyz);
    
    // Transform to clip space (row vector * matrix for row-major)
    gl_Position = worldPosition * u_ViewProjectionMatrix;
}
