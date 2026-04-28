#version 450

/**
 * FXAA Fragment Shader (NVIDIA FXAA 3.11)
 * 
 * Implements Fast Approximate Anti-Aliasing algorithm.
 * Reference: UE5 FXAAShader.usf, Fxaa3_11.ush
 * 
 * Algorithm Overview:
 *   1. Sample 3x3 neighborhood and compute luma
 *   2. Detect edges using luma differences
 *   3. Determine edge direction (horizontal/vertical)
 *   4. Search along edge for endpoints
 *   5. Compute blend factor based on edge characteristics
 *   6. Sample along edge and blend with center pixel
 *   7. Apply sub-pixel anti-aliasing
 * 
 * Input: Color texture from Lighting Pass (LDR, non-linear colorspace)
 * Output: Anti-aliased color
 */

// ============================================================================
// Configuration (matches Preset 2: QUALITY__PRESET 13)
// ============================================================================

#define FXAA_PC 1
#define FXAA_QUALITY__PRESET 13
#define FXAA_GREEN_AS_LUMA 1  // Use green channel as luma (simplified)

// ============================================================================
// Inputs / Outputs
// ============================================================================

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniforms
// ============================================================================

layout(set = 0, binding = 0) uniform FXAAParams {
    vec2 RcpFrame;                  // 1.0 / (width, height)
    float QualitySubpix;            // Sub-pixel quality (0.0 - 1.0)
    float QualityEdgeThreshold;     // Edge detection threshold
    float QualityEdgeThresholdMin;  // Min edge threshold
    int Preset;                     // Quality preset (unused, baked at compile time)
} fxaaParams;

layout(set = 0, binding = 1) uniform sampler2D InputTexture;

// ============================================================================
// FXAA Quality Preset Configuration
// ============================================================================

// Preset 13 (Balanced quality/performance)
#if (FXAA_QUALITY__PRESET == 13)
    #define FXAA_QUALITY__PS 5
    #define FXAA_QUALITY__P0 1.0
    #define FXAA_QUALITY__P1 1.5
    #define FXAA_QUALITY__P2 2.0
    #define FXAA_QUALITY__P3 2.0
    #define FXAA_QUALITY__P4 4.0
#endif

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Compute luma from RGB color
 * Uses green channel as proxy when FXAA_GREEN_AS_LUMA is enabled
 */
float FxaaLuma(vec3 rgb)
{
#if (FXAA_GREEN_AS_LUMA == 1)
    return rgb.g;  // Use green channel as luma
#else
    return dot(rgb, vec3(0.299, 0.587, 0.114));  // Standard luma calculation
#endif
}

/**
 * Sample texture with offset
 */
vec4 FxaaTexOff(sampler2D tex, vec2 pos, ivec2 off, vec2 rcpFrame)
{
    return texture(tex, pos + vec2(off) * rcpFrame);
}

// ============================================================================
// FXAA Main Algorithm
// ============================================================================

/**
 * FXAA Pixel Shader (Quality PC version)
 * 
 * @param pos Current pixel UV coordinate
 * @param tex Input texture
 * @param rcpFrame Reciprocal frame size (1.0 / resolution)
 * @param qualitySubpix Sub-pixel aliasing removal amount
 * @param qualityEdgeThreshold Edge detection threshold
 * @param qualityEdgeThresholdMin Minimum edge threshold
 * @return Anti-aliased color
 */
vec4 FxaaPixelShader(
    vec2 pos,
    sampler2D tex,
    vec2 rcpFrame,
    float qualitySubpix,
    float qualityEdgeThreshold,
    float qualityEdgeThresholdMin
)
{
    // ========================================================================
    // Step 1: Sample 3x3 neighborhood
    // ========================================================================
    
    vec3 rgbN  = FxaaTexOff(tex, pos, ivec2( 0,-1), rcpFrame).rgb;
    vec3 rgbW  = FxaaTexOff(tex, pos, ivec2(-1, 0), rcpFrame).rgb;
    vec3 rgbM  = texture(tex, pos).rgb;  // Center pixel
    vec3 rgbE  = FxaaTexOff(tex, pos, ivec2( 1, 0), rcpFrame).rgb;
    vec3 rgbS  = FxaaTexOff(tex, pos, ivec2( 0, 1), rcpFrame).rgb;
    
    // Compute luma for each sample
    float lumaN = FxaaLuma(rgbN);
    float lumaW = FxaaLuma(rgbW);
    float lumaM = FxaaLuma(rgbM);
    float lumaE = FxaaLuma(rgbE);
    float lumaS = FxaaLuma(rgbS);
    
    // ========================================================================
    // Step 2: Detect edges
    // ========================================================================
    
    float rangeMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
    float range = rangeMax - rangeMin;
    
    // Early exit: no edge detected
    if (range < max(qualityEdgeThresholdMin, rangeMax * qualityEdgeThreshold))
    {
        return vec4(rgbM, 1.0);
    }
    
    // ========================================================================
    // Step 3: Sample diagonal neighbors
    // ========================================================================
    
    vec3 rgbNW = FxaaTexOff(tex, pos, ivec2(-1,-1), rcpFrame).rgb;
    vec3 rgbNE = FxaaTexOff(tex, pos, ivec2( 1,-1), rcpFrame).rgb;
    vec3 rgbSW = FxaaTexOff(tex, pos, ivec2(-1, 1), rcpFrame).rgb;
    vec3 rgbSE = FxaaTexOff(tex, pos, ivec2( 1, 1), rcpFrame).rgb;
    
    float lumaNW = FxaaLuma(rgbNW);
    float lumaNE = FxaaLuma(rgbNE);
    float lumaSW = FxaaLuma(rgbSW);
    float lumaSE = FxaaLuma(rgbSE);
    
    // ========================================================================
    // Step 4: Determine edge direction
    // ========================================================================
    
    float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25;
    float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0, (rangeL / range) - qualitySubpix) / (1.0 - qualitySubpix);
    
    // Compute edge direction weights
    float edgeVert = 
        abs((0.25 * lumaNW) + (-0.5 * lumaN) + (0.25 * lumaNE)) +
        abs((0.50 * lumaW ) + (-1.0 * lumaM) + (0.50 * lumaE )) +
        abs((0.25 * lumaSW) + (-0.5 * lumaS) + (0.25 * lumaSE));
    
    float edgeHorz = 
        abs((0.25 * lumaNW) + (-0.5 * lumaW) + (0.25 * lumaSW)) +
        abs((0.50 * lumaN ) + (-1.0 * lumaM) + (0.50 * lumaS )) +
        abs((0.25 * lumaNE) + (-0.5 * lumaE) + (0.25 * lumaSE));
    
    bool horzSpan = edgeHorz >= edgeVert;
    
    // ========================================================================
    // Step 5: Choose edge orientation and search direction
    // ========================================================================
    
    float lengthSign = horzSpan ? -rcpFrame.y : -rcpFrame.x;
    
    if (!horzSpan)
    {
        lumaN = lumaW;
        lumaS = lumaE;
    }
    
    float gradientN = abs(lumaN - lumaM);
    float gradientS = abs(lumaS - lumaM);
    
    lumaN = (lumaN + lumaM) * 0.5;
    lumaS = (lumaS + lumaM) * 0.5;
    
    // Choose search direction
    bool pairN = gradientN >= gradientS;
    if (!pairN)
    {
        lumaN = lumaS;
        gradientN = gradientS;
        lengthSign = -lengthSign;
    }
    
    // ========================================================================
    // Step 6: Search along edge
    // ========================================================================
    
    vec2 posN;
    posN.x = pos.x + (horzSpan ? 0.0 : lengthSign * 0.5);
    posN.y = pos.y + (horzSpan ? lengthSign * 0.5 : 0.0);
    
    gradientN *= 0.25;
    
    vec2 posP = posN;
    vec2 offNP = horzSpan ? vec2(rcpFrame.x, 0.0) : vec2(0.0, rcpFrame.y);
    
    float lumaEndN = lumaN;
    float lumaEndP = lumaN;
    
    bool doneN = false;
    bool doneP = false;
    
    // Iterative search along edge (quality preset determines iterations)
    posN += offNP * vec2(-1.0, -1.0);
    posP += offNP * vec2( 1.0,  1.0);
    
    for (int i = 0; i < FXAA_QUALITY__PS; i++)
    {
        if (!doneN) lumaEndN = FxaaLuma(texture(tex, posN).rgb);
        if (!doneP) lumaEndP = FxaaLuma(texture(tex, posP).rgb);
        
        doneN = doneN || (abs(lumaEndN - lumaN) >= gradientN);
        doneP = doneP || (abs(lumaEndP - lumaN) >= gradientN);
        
        if (doneN && doneP) break;
        
        if (!doneN) posN -= offNP;
        if (!doneP) posP += offNP;
    }
    
    // ========================================================================
    // Step 7: Compute blend factor and final color
    // ========================================================================
    
    float dstN = horzSpan ? (pos.x - posN.x) : (pos.y - posN.y);
    float dstP = horzSpan ? (posP.x - pos.x) : (posP.y - pos.y);
    
    bool directionN = dstN < dstP;
    lumaEndN = directionN ? lumaEndN : lumaEndP;
    
    if (((lumaM - lumaN) < 0.0) == ((lumaEndN - lumaN) < 0.0))
    {
        lengthSign = 0.0;
    }
    
    float spanLength = (dstP + dstN);
    dstN = directionN ? dstN : dstP;
    float subPixelOffset = (0.5 + (dstN * (-1.0 / spanLength))) * lengthSign;
    
    // Sample final color with offset
    vec3 rgbF = texture(tex, vec2(
        pos.x + (horzSpan ? 0.0 : subPixelOffset),
        pos.y + (horzSpan ? subPixelOffset : 0.0)
    )).rgb;
    
    // Blend with sub-pixel AA
    return vec4(mix(rgbF, rgbM, blendL), 1.0);
}

// ============================================================================
// Main Entry Point
// ============================================================================

void main()
{
    outColor = FxaaPixelShader(
        inUV,
        InputTexture,
        fxaaParams.RcpFrame,
        fxaaParams.QualitySubpix,
        fxaaParams.QualityEdgeThreshold,
        fxaaParams.QualityEdgeThresholdMin
    );
}
