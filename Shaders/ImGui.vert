#version 450

// ImGui Vertex Shader for Vulkan
// Transforms 2D UI vertices and passes through UV and color

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBuffer {
    mat4 projectionMatrix;
} ubo;

void main() {
    outTexCoord = inTexCoord;
    outColor = inColor;
    gl_Position = ubo.projectionMatrix * vec4(inPosition, 0.0, 1.0);
}
