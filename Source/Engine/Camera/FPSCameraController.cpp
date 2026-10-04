// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FPSCameraController.cpp
 * @brief Implementation of FPS-style camera controller
 * 
 * Reference: LearnOpenGL Camera class
 * Reference UE5: Engine/Source/Runtime/Engine/Private/PlayerController.cpp
 */

#include "Engine/Camera/FPSCameraController.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/CameraTypes.h"
#include "Core/Logging/LogMacros.h"

namespace MonsterEngine
{

// Use global log category
using MonsterRender::LogCameraManager;

// ============================================================================
// Default Values (matching LearnOpenGL)
// ============================================================================

static constexpr float DEFAULT_YAW = -90.0f;
static constexpr float DEFAULT_PITCH = 0.0f;
static constexpr float DEFAULT_FOV = 45.0f;
static constexpr float DEFAULT_SPEED = 5.0f;
static constexpr float DEFAULT_SENSITIVITY = 0.1f;

// ============================================================================
// Construction / Destruction
// ============================================================================

FFPSCameraController::FFPSCameraController()
    : m_position(FVector::ZeroVector)
    , m_front(FVector(0.0, 0.0, -1.0))
    , m_up(FVector(0.0, 1.0, 0.0))
    , m_right(FVector(1.0, 0.0, 0.0))
    , m_worldUp(FVector(0.0, 1.0, 0.0))
    , m_yaw(DEFAULT_YAW)
    , m_pitch(DEFAULT_PITCH)
    , m_roll(0.0f)
    , m_fov(DEFAULT_FOV)
    , m_settings()
    , m_cameraManager(nullptr)
    , m_bEnabled(true)
    , m_bFirstMouse(true)
    , m_lastMouseX(0.0f)
    , m_lastMouseY(0.0f)
{
    // Initialize with default settings
    m_settings.MovementSpeed = DEFAULT_SPEED;
    m_settings.MouseSensitivity = DEFAULT_SENSITIVITY;
    
    // Calculate initial camera vectors
    _updateCameraVectors();
}

FFPSCameraController::FFPSCameraController(
    const FVector& InPosition,
    const FVector& InWorldUp,
    float InYaw,
    float InPitch)
    : m_position(InPosition)
    , m_front(FVector(0.0, 0.0, -1.0))
    , m_up(FVector(0.0, 1.0, 0.0))
    , m_right(FVector(1.0, 0.0, 0.0))
    , m_worldUp(InWorldUp)
    , m_yaw(InYaw)
    , m_pitch(InPitch)
    , m_roll(0.0f)
    , m_fov(DEFAULT_FOV)
    , m_settings()
    , m_cameraManager(nullptr)
    , m_bEnabled(true)
    , m_bFirstMouse(true)
    , m_lastMouseX(0.0f)
    , m_lastMouseY(0.0f)
{
    // Initialize with default settings
    m_settings.MovementSpeed = DEFAULT_SPEED;
    m_settings.MouseSensitivity = DEFAULT_SENSITIVITY;
    
    // Calculate initial camera vectors from Euler angles
    _updateCameraVectors();
    
    MR_LOG(LogCameraManager, Log, "FPS Camera initialized: pos=(%.2f, %.2f, %.2f), yaw=%.1f, pitch=%.1f",
           m_position.X, m_position.Y, m_position.Z, m_yaw, m_pitch);
}

FFPSCameraController::~FFPSCameraController()
{
    m_cameraManager = nullptr;
}

// ============================================================================
// Initialization
// ============================================================================

void FFPSCameraController::Initialize(FCameraManager* InCameraManager)
{
    m_cameraManager = InCameraManager;
    m_bFirstMouse = true;
    
    // Apply initial state to camera manager
    if (m_cameraManager)
    {
        _applyCameraState();
    }
    
    MR_LOG(LogCameraManager, Log, "FPS Camera Controller initialized");
}

void FFPSCameraController::Reset()
{
    m_position = FVector::ZeroVector;
    m_yaw = DEFAULT_YAW;
    m_pitch = DEFAULT_PITCH;
    m_roll = 0.0f;
    m_fov = DEFAULT_FOV;
    m_bFirstMouse = true;
    
    _updateCameraVectors();
    
    MR_LOG(LogCameraManager, Log, "FPS Camera Controller reset");
}

// ============================================================================
// Input Processing
// ============================================================================

void FFPSCameraController::ProcessKeyboard(ECameraMovement Direction, float DeltaTime, bool bSprinting)
{
    if (!m_bEnabled)
    {
        return;
    }
    
    // Calculate velocity with optional sprint multiplier
    float velocity = m_settings.MovementSpeed * DeltaTime;
    if (bSprinting)
    {
        velocity *= m_settings.SprintMultiplier;
    }
    
    // Move camera based on direction
    switch (Direction)
    {
        case ECameraMovement::Forward:
            m_position = m_position + m_front * velocity;
            break;
            
        case ECameraMovement::Backward:
            m_position = m_position - m_front * velocity;
            break;
            
        case ECameraMovement::Left:
            m_position = m_position - m_right * velocity;
            break;
            
        case ECameraMovement::Right:
            m_position = m_position + m_right * velocity;
            break;
            
        case ECameraMovement::Up:
            m_position = m_position + (m_settings.bLocalVerticalMovement ? m_up : m_worldUp) * velocity;
            break;
            
        case ECameraMovement::Down:
            m_position = m_position - (m_settings.bLocalVerticalMovement ? m_up : m_worldUp) * velocity;
            break;
    }
}

void FFPSCameraController::ProcessMouseMovement(float XOffset, float YOffset, bool bConstrainPitch)
{
    if (!m_bEnabled)
    {
        return;
    }
    
    // Apply sensitivity
    XOffset *= m_settings.MouseSensitivity;
    YOffset *= m_settings.MouseSensitivity;
    
    // Update Euler angles
    m_yaw += XOffset;
    
    // Invert Y if configured
    if (m_settings.bInvertY)
    {
        m_pitch -= YOffset;
    }
    else
    {
        m_pitch += YOffset;
    }
    
    // Constrain pitch to prevent screen flip
    bool shouldConstrain = bConstrainPitch && m_settings.bConstrainPitch;
    if (shouldConstrain)
    {
        m_pitch = FMath::Clamp(m_pitch, m_settings.MinPitch, m_settings.MaxPitch);
    }
    
    // Update camera vectors from new Euler angles
    _updateCameraVectors();
}

void FFPSCameraController::ProcessMouseScroll(float YOffset)
{
    if (!m_bEnabled)
    {
        return;
    }
    
    // Adjust FOV (zoom)
    m_fov -= YOffset * m_settings.ScrollSensitivity;
    m_fov = FMath::Clamp(m_fov, m_settings.MinFOV, m_settings.MaxFOV);
}

void FFPSCameraController::ProcessRoll(float DegreesPerSecond, float DeltaTime)
{
    if (!m_bEnabled)
    {
        return;
    }

    SetRoll(m_roll + DegreesPerSecond * DeltaTime);
}

// ============================================================================
// Update
// ============================================================================

void FFPSCameraController::Update(float DeltaTime)
{
    if (!m_bEnabled)
    {
        return;
    }
    
    // Apply camera state to camera manager
    _applyCameraState();
}

// ============================================================================
// View Matrix
// ============================================================================

FMatrix FFPSCameraController::GetViewMatrix() const
{
    // Calculate look-at target
    FVector target = m_position + m_front;
    
    // Create view matrix using LookAt
    return FMatrix::MakeLookAt(m_position, target, m_up);
}

// ============================================================================
// Setters
// ============================================================================

void FFPSCameraController::SetPosition(const FVector& InPosition)
{
    m_position = InPosition;
}

void FFPSCameraController::SetRotation(float InYaw, float InPitch)
{
    m_yaw = InYaw;
    
    // Constrain pitch if enabled
    if (m_settings.bConstrainPitch)
    {
        m_pitch = FMath::Clamp(InPitch, m_settings.MinPitch, m_settings.MaxPitch);
    }
    else
    {
        m_pitch = InPitch;
    }
    
    // Update camera vectors
    _updateCameraVectors();
}

void FFPSCameraController::SetRotation(float InYaw, float InPitch, float InRoll)
{
    m_roll = InRoll;
    SetRotation(InYaw, InPitch);
}

void FFPSCameraController::SetRoll(float InRoll)
{
    m_roll = InRoll;
    _updateCameraVectors();
}

void FFPSCameraController::SetFOV(float InFOV)
{
    m_fov = FMath::Clamp(InFOV, m_settings.MinFOV, m_settings.MaxFOV);
}

void FFPSCameraController::SetSettings(const FFPSCameraSettings& InSettings)
{
    m_settings = InSettings;
}

void FFPSCameraController::SetCameraManager(FCameraManager* InCameraManager)
{
    m_cameraManager = InCameraManager;
}

void FFPSCameraController::LookAt(const FVector& Target)
{
    // Calculate direction to target
    FVector direction = Target - m_position;
    direction = direction.GetSafeNormal();
    
    // Calculate yaw and pitch from direction
    // Yaw: angle in XZ plane from -Z axis
    m_yaw = FMath::RadiansToDegrees(FMath::Atan2(direction.Z, direction.X));
    
    // Pitch: angle from XZ plane
    m_pitch = FMath::RadiansToDegrees(FMath::Asin(direction.Y));
    
    // Constrain pitch
    if (m_settings.bConstrainPitch)
    {
        m_pitch = FMath::Clamp(m_pitch, m_settings.MinPitch, m_settings.MaxPitch);
    }
    
    // Update camera vectors
    _updateCameraVectors();
}

// ============================================================================
// Internal Methods
// ============================================================================

void FFPSCameraController::_updateCameraVectors()
{
    // Calculate new front vector from Euler angles
    // Reference: LearnOpenGL camera.h updateCameraVectors()
    FVector front;
    float yawRad = FMath::DegreesToRadians(m_yaw);
    float pitchRad = FMath::DegreesToRadians(m_pitch);
    
    front.X = FMath::Cos(yawRad) * FMath::Cos(pitchRad);
    front.Y = FMath::Sin(pitchRad);
    front.Z = FMath::Sin(yawRad) * FMath::Cos(pitchRad);
    
    m_front = front.GetSafeNormal();
    
    // Re-calculate right and up vectors
    // Right = normalize(cross(Front, WorldUp))
    const FVector unrolledRight = FVector::CrossProduct(m_front, m_worldUp).GetSafeNormal();
    
    // Up = normalize(cross(Right, Front))
    const FVector unrolledUp = FVector::CrossProduct(unrolledRight, m_front).GetSafeNormal();

    // Rotate the orthonormal view basis around the front vector. A zero roll
    // preserves the previous FPS camera behavior exactly.
    const float rollRad = FMath::DegreesToRadians(m_roll);
    const float cosine = FMath::Cos(rollRad);
    const float sine = FMath::Sin(rollRad);
    m_right = unrolledRight * cosine - unrolledUp * sine;
    m_up = unrolledUp * cosine + unrolledRight * sine;
}

void FFPSCameraController::_applyCameraState()
{
    if (!m_cameraManager)
    {
        return;
    }
    
    // Get current camera view info
    FMinimalViewInfo viewInfo = m_cameraManager->GetCameraCacheView();
    
    // Update position
    viewInfo.Location = m_position;
    
    // Calculate rotation from front vector
    // Convert front direction to rotator
    FRotator rotation;
    rotation.Pitch = m_pitch;
    rotation.Yaw = m_yaw;
    rotation.Roll = m_roll;
    viewInfo.Rotation = rotation;
    
    // Update FOV
    viewInfo.FOV = m_fov;
    
    // Apply to camera manager
    m_cameraManager->SetCameraCachePOV(viewInfo);
    m_cameraManager->SetViewTargetPOV(viewInfo);
}

} // namespace MonsterEngine
