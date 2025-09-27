#version 450

// Fragment input
layout(location = 0) in vec3 fragColor;

// Fragment output
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
