// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <cmath>
#include <algorithm>

namespace MonsterRender::Splat
{

/** One validated snapshot of camera and 3D Gaussian rendering controls. Angles are degrees. */
struct FSplatControlState
{
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    float PositionZ = 0.0f;
    float Yaw = -90.0f;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    float FOV = 45.0f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    float MouseSensitivity = 0.1f;
    float MovementSpeed = 5.0f;
    float GaussianScale = 1.0f;
    bool bDistanceCulling = true;
    bool bWireframe = false;
    bool bLODEnabled = true; ///< Streaming selector may refine when enabled.
    bool bFreezeLOD = false; ///< Preserve cut while still enforcing readiness.
    bool bLODLevelOverlay = false; ///< Color support outlines by selected level.
    bool bLODBoundsOverlay = false; ///< Draw selected support bounds.
    float LODQualityPixels = 1.0f; ///< Projected error threshold.

    bool IsValid() const
    {
        return std::isfinite(PositionX) && std::isfinite(PositionY) && std::isfinite(PositionZ)
            && std::isfinite(Yaw) && std::isfinite(Pitch) && std::isfinite(Roll)
            && Pitch >= -89.0f && Pitch <= 89.0f
            && std::isfinite(FOV) && FOV >= 30.0f && FOV <= 120.0f
            && std::isfinite(NearPlane) && std::isfinite(FarPlane)
            && NearPlane > 0.0f && FarPlane > NearPlane
            && std::isfinite(MouseSensitivity) && MouseSensitivity >= 0.01f && MouseSensitivity <= 0.4f
            && std::isfinite(MovementSpeed) && MovementSpeed >= 1.0f && MovementSpeed <= 10.0f
            && std::isfinite(GaussianScale) && GaussianScale >= 0.01f && GaussianScale <= 10.0f
            && std::isfinite(LODQualityPixels) && LODQualityPixels > 0;
    }

    void ClampToValid()
    {
        const FSplatControlState defaults;
        auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
        PositionX = finiteOr(PositionX, defaults.PositionX);
        PositionY = finiteOr(PositionY, defaults.PositionY);
        PositionZ = finiteOr(PositionZ, defaults.PositionZ);
        Yaw = finiteOr(Yaw, defaults.Yaw);
        Pitch = std::clamp(finiteOr(Pitch, defaults.Pitch), -89.0f, 89.0f);
        Roll = finiteOr(Roll, defaults.Roll);
        FOV = std::clamp(finiteOr(FOV, defaults.FOV), 30.0f, 120.0f);
        NearPlane = std::max(finiteOr(NearPlane, defaults.NearPlane), 0.0001f);
        FarPlane = finiteOr(FarPlane, defaults.FarPlane);
        if (FarPlane <= NearPlane)
            FarPlane = NearPlane + std::max(1.0f, NearPlane * 0.01f);
        MouseSensitivity = std::clamp(finiteOr(MouseSensitivity, defaults.MouseSensitivity), 0.01f, 0.4f);
        MovementSpeed = std::clamp(finiteOr(MovementSpeed, defaults.MovementSpeed), 1.0f, 10.0f);
        GaussianScale = std::clamp(finiteOr(GaussianScale, defaults.GaussianScale), 0.01f, 10.0f);
        LODQualityPixels = std::clamp(finiteOr(LODQualityPixels, defaults.LODQualityPixels), 0.001f, 1000.0f);
    }
};

} // namespace MonsterRender::Splat
