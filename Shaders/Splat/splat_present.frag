#version 450
// Fullscreen fragment shader for presenting SplatPipeline output to swapchain.
// Samples the RGBA8 storage image output from the splat render pass.

layout(set = 0, binding = 0) uniform sampler2D splatOutput;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 color)
{
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}

vec4 sampleCatmullRom(vec2 uv)
{
    vec2 size = vec2(textureSize(splatOutput, 0));
    vec2 position = uv * size - 0.5;
    vec2 base = floor(position);
    vec2 f = position - base;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;

    vec2 w0 = -0.5 * f + f2 - 0.5 * f3;
    vec2 w1 = 1.0 - 2.5 * f2 + 1.5 * f3;
    vec2 w2 = 0.5 * f + 2.0 * f2 - 1.5 * f3;
    vec2 w3 = -0.5 * f2 + 0.5 * f3;
    vec2 invSize = 1.0 / size;

    vec4 color = vec4(0.0);
    for (int y = 0; y < 4; ++y)
    {
        float wy = y == 0 ? w0.y : y == 1 ? w1.y : y == 2 ? w2.y : w3.y;
        for (int x = 0; x < 4; ++x)
        {
            float wx = x == 0 ? w0.x : x == 1 ? w1.x : x == 2 ? w2.x : w3.x;
            vec2 tap = (base + vec2(x - 1, y - 1) + 0.5) * invSize;
            color += textureLod(splatOutput, tap, 0.0) * (wx * wy);
        }
    }
    return color;
}

void main()
{
    vec2 sourceSize = vec2(textureSize(splatOutput, 0));
    vec2 outputSize = 1.0 / max(fwidth(fragUV), vec2(1e-6));
    if (all(lessThanEqual(outputSize, sourceSize + vec2(1.0))))
    {
        vec4 color = texture(splatOutput, fragUV);
        outColor = vec4(srgbToLinear(max(color.rgb, vec3(0.0))), color.a);
        return;
    }

    // A single-pass edge-preserving upscale for the presentation path:
    // Catmull-Rom reconstruction followed by a restrained RCAS-style local
    // contrast recovery. Native-resolution output keeps the direct path above.
    vec4 reconstructed = sampleCatmullRom(fragUV);
    vec2 texel = 1.0 / sourceSize;
    vec3 north = textureLod(splatOutput, fragUV - vec2(0.0, texel.y), 0.0).rgb;
    vec3 south = textureLod(splatOutput, fragUV + vec2(0.0, texel.y), 0.0).rgb;
    vec3 west  = textureLod(splatOutput, fragUV - vec2(texel.x, 0.0), 0.0).rgb;
    vec3 east  = textureLod(splatOutput, fragUV + vec2(texel.x, 0.0), 0.0).rgb;
    vec3 minimumColor = min(reconstructed.rgb, min(min(north, south), min(west, east)));
    vec3 maximumColor = max(reconstructed.rgb, max(max(north, south), max(west, east)));
    float contrast = max(maximumColor.r - minimumColor.r,
                         max(maximumColor.g - minimumColor.g, maximumColor.b - minimumColor.b));
    float gain = 0.22 * smoothstep(0.015, 0.20, contrast);
    vec3 detail = reconstructed.rgb - 0.25 * (north + south + west + east);
    vec3 sharpened = clamp(reconstructed.rgb + gain * detail, minimumColor, maximumColor);
    outColor = vec4(srgbToLinear(max(sharpened, vec3(0.0))), reconstructed.a);
}
