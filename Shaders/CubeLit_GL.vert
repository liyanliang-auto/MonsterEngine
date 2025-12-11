#version 330 core

// Vertex shader for lit cube rendering (OpenGL version)
// Supports multiple lights with Blinn-Phong shading

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniform matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform vec3 cameraPosition;

// Outputs to fragment shader
out vec3 fragWorldPos;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec3 fragViewPos;

void main() {
    // Transform position to world space
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space using normal matrix
    fragNormal = mat3(normalMatrix) * inNormal;
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Camera position for specular calculations
    fragViewPos = cameraPosition;
    
    // Final clip space position
    gl_Position = projection * view * worldPos;
}
