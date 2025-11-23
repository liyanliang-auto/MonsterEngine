#version 450
// Cube Fragment Shader
// Reference: LearnOpenGL Coordinate Systems tutorial

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

// Texture samplers
layout(binding = 1) uniform sampler2D texture1;  // Container texture
layout(binding = 2) uniform sampler2D texture2;  // Awesomeface texture

void main() {
    // Sample both textures
    vec4 color1 = texture(texture1, fragTexCoord);
    vec4 color2 = texture(texture2, fragTexCoord);
    
    // Mix textures: 80% container + 20% awesomeface
    // Matching LearnOpenGL tutorial: mix(texture1, texture2, 0.2)
    outColor = mix(color1, color2, 0.2);
}
