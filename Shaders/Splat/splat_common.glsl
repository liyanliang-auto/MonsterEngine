// Copyright Monster Engine. All Rights Reserved.
// splat_common.glsl - Common math functions for 3DGS splat rendering
// Reference: 3dgs-vulkan-cpp preprocess.comp

#ifndef SPLAT_COMMON_GLSL
#define SPLAT_COMMON_GLSL

// ============================================================================
// Spherical Harmonics Constants
// ============================================================================
#define SH_C0 0.28209479177387814
const float SH_C1 = 0.4886025119029199;
const float SH_C2[5] = float[](
    1.0925484305920792,
    -1.0925484305920792,
    0.31539156525252005,
    -1.0925484305920792,
    0.5462742152960396
);
const float SH_C3[7] = float[](
    -0.5900435899266435,
    2.890611442640554,
    -0.4570457994644658,
    0.3731763325901154,
    -0.4570457994644658,
    1.445305721320277,
    -0.5900435899266435
);

// ============================================================================
// SH coefficient count per degree
// ============================================================================
int getSHCoeffCount(int degree) {
    if (degree == 0) return 1;
    if (degree == 1) return 4;
    if (degree == 2) return 9;
    if (degree == 3) return 16;
    return 1;
}

// ============================================================================
// NDC to pixel coordinate conversion
// ============================================================================
float ndc2Pix(float ndc, int size) {
    return ((ndc + 1.0) * size - 1.0) * 0.5;
}

// ============================================================================
// Compute 3D covariance matrix from scale + rotation (quaternion)
// Output: cov3D[6] = {xx, xy, xz, yy, yz, zz} (upper triangle)
// Reference: preprocess.comp::computeCov3D
// ============================================================================
void computeCov3D(vec3 scale, vec4 rot, float scaleModifier, out float cov3D_out[6]) {
    // Scaling matrix
    mat3 S = mat3(
        scaleModifier * scale.x, 0.0, 0.0,
        0.0, scaleModifier * scale.y, 0.0,
        0.0, 0.0, scaleModifier * scale.z
    );

    // Normalize quaternion and compute rotation matrix
    vec4 q = normalize(rot);
    float r = q.x, x = q.y, y = q.z, z = q.w;

    mat3 R = mat3(
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - r * z), 2.0 * (x * z + r * y),
        2.0 * (x * y + r * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - r * x),
        2.0 * (x * z - r * y), 2.0 * (y * z + r * x), 1.0 - 2.0 * (x * x + y * y)
    );

    mat3 M = S * R;
    mat3 Sigma = transpose(M) * M;

    // Store symmetric matrix (upper triangle)
    cov3D_out[0] = Sigma[0][0]; // xx
    cov3D_out[1] = Sigma[0][1]; // xy
    cov3D_out[2] = Sigma[0][2]; // xz
    cov3D_out[3] = Sigma[1][1]; // yy
    cov3D_out[4] = Sigma[1][2]; // yz
    cov3D_out[5] = Sigma[2][2]; // zz
}

// ============================================================================
// Frustum culling
// Reference: preprocess.comp::inFrustum
// ============================================================================
bool inFrustum(vec3 posWorld, mat4 viewMatrix, mat4 projMatrix, float nearPlane, float farPlane, out vec3 pView) {
    pView = mat3(viewMatrix) * posWorld + viewMatrix[3].xyz;

    if (pView.z >= -nearPlane) return false;
    if (pView.z < -farPlane)   return false;

    vec4 pClip = projMatrix * vec4(pView, 1.0);
    return abs(pClip.x) <= pClip.w && abs(pClip.y) <= pClip.w;
}

// ============================================================================
// Jacobian projection + 2D covariance + low-pass filter
// Returns vec3(cov2D_xx, cov2D_xy, cov2D_yy)
// Reference: preprocess.comp::computeCov2D
// ============================================================================
vec3 computeCov2D(vec3 pView, float cov3D_data[6], mat4 viewMatrix, float focalX, float focalY, float tanFovX, float tanFovY) {
    vec3 t = pView;

    // Clamp to frustum
    float limx = 1.3 * tanFovX;
    float limy = 1.3 * tanFovY;
    float txtz = t.x / t.z;
    float tytz = t.y / t.z;
    t.x = clamp(txtz, -limx, limx) * t.z;
    t.y = clamp(tytz, -limy, limy) * t.z;

    // Jacobian matrix
    mat3 J = mat3(
        focalX / t.z, 0.0, -(focalX * t.x) / (t.z * t.z),
        0.0, focalY / t.z, -(focalY * t.y) / (t.z * t.z),
        0.0, 0.0, 0.0
    );

    // View matrix upper 3x3 (rotation)
    mat3 W = transpose(mat3(viewMatrix));
    mat3 T = W * J;

    // 3D covariance as mat3
    mat3 Vrk = mat3(
        cov3D_data[0], cov3D_data[1], cov3D_data[2],
        cov3D_data[1], cov3D_data[3], cov3D_data[4],
        cov3D_data[2], cov3D_data[4], cov3D_data[5]
    );

    mat3 cov = transpose(T) * Vrk * T;

    // Low-pass filter
    cov[0][0] += 0.3;
    cov[1][1] += 0.3;

    return vec3(cov[0][0], cov[0][1], cov[1][1]);
}

// ============================================================================
// Note: computeColorFromSH is inlined in splat_preprocess.comp because it
// requires direct access to the shCoefficients[] storage buffer (unsized
// arrays cannot be passed as function parameters in Vulkan GLSL).
// ============================================================================

#endif // SPLAT_COMMON_GLSL
