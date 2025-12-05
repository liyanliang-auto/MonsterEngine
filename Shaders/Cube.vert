#version 450
// Cube Vertex Shader
// Reference: LearnOpenGL Coordinate Systems tutorial

// Input vertex attributes
layout(location = 0) in vec3 inPosition;    // Vertex position
layout(location = 1) in vec2 inTexCoord;    // Texture coordinates

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;

// Uniform buffer object (MVP matrices) - explicitly use set=0
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;         // Model matrix (object -> world)
    mat4 view;          // View matrix (world -> camera)
    mat4 projection;    // Projection matrix (camera -> clip space)
} ubo;

void main() {
    // Transform vertex position: Model -> View -> Projection
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    // Pass texture coordinates to fragment shader
    fragTexCoord = inTexCoord;
}
