#version 460 core

/**
 * Cube_GL.vert
 * 
 * OpenGL 4.6 vertex shader for textured cube rendering
 * Reference: LearnOpenGL Coordinate Systems tutorial
 */

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Uniform buffer for MVP matrices (std140 layout)
layout(std140, binding = 0) uniform MVPMatrices {
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Transform vertex position through MVP matrices
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    // Pass texture coordinates to fragment shader
    fragTexCoord = inTexCoord;
}
