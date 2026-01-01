// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FPSCameraController.h
 * @brief FPS-style camera controller with WASD movement and mouse look
 * 
 * FFPSCameraController provides first-person shooter style camera control:
 * - WASD keyboard movement
 * - Mouse look (yaw/pitch)
 * - Mouse scroll zoom (FOV adjustment)
 * - Configurable movement speed and sensitivity
 * 
 * Reference: LearnOpenGL Camera class
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h
 */

#include "Core/CoreTypes.h"
#include "Math/MonsterMath.h"

namespace MonsterEngine
{

// Forward declarations
class FCameraManager;
struct FMinimalViewInfo;

// ============================================================================
// ECameraMovement
// ============================================================================

/**
 * Camera movement direction enumeration
 * Used to abstract input from window system specific key codes
 */
enum class ECameraMovement : uint8
{
    Forward = 0,
    Backward,
    Left,
    Right,
    Up,
    Down
};

// ============================================================================
// FFPSCameraSettings
// ============================================================================

/**
 * FPS camera configuration settings
 */
struct FFPSCameraSettings
{
    /** Movement speed in units per second */
    float MovementSpeed = 5.0f;
    
    /** Mouse sensitivity for look rotation */
    float MouseSensitivity = 0.1f;
    
    /** Scroll sensitivity for zoom */
    float ScrollSensitivity = 2.0f;
    
    /** Minimum FOV (max zoom in) */
    float MinFOV = 1.0f;
    
    /** Maximum FOV (max zoom out) */
    float MaxFOV = 90.0f;
    
    /** Minimum pitch angle (looking down) */
    float MinPitch = -89.0f;
    
    /** Maximum pitch angle (looking up) */
    float MaxPitch = 89.0f;
    
    /** Whether to constrain pitch */
    bool bConstrainPitch = true;
    
    /** Whether to invert Y axis for mouse look */
    bool bInvertY = false;
    
    /** Sprint speed multiplier */
    float SprintMultiplier = 2.0f;
    
    /** Default constructor */
    FFPSCameraSettings() = default;
};

// ============================================================================
// FFPSCameraController
// ============================================================================

/**
 * FPS-style camera controller
 * 
 * Provides first-person shooter style camera control with:
 * - WASD movement relative to camera facing direction
 * - Mouse look for yaw (horizontal) and pitch (vertical) rotation
 * - Mouse scroll for FOV zoom
 * - Configurable speed and sensitivity
 * 
 * Usage:
 * 1. Create controller and attach to camera manager
 * 2. Call ProcessKeyboard() for WASD input
 * 3. Call ProcessMouseMovement() for mouse look
 * 4. Call ProcessMouseScroll() for zoom
 * 5. Call Update() each frame to apply changes
 * 
 * Thread Safety: Not thread-safe, should be called from main thread only
 */
class FFPSCameraController
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /** Default constructor */
    FFPSCameraController();
    
    /**
     * Constructor with initial position
     * @param InPosition Initial camera position
     * @param InWorldUp World up vector (default Y-up)
     * @param InYaw Initial yaw angle in degrees
     * @param InPitch Initial pitch angle in degrees
     */
    FFPSCameraController(
        const FVector& InPosition,
        const FVector& InWorldUp = FVector(0.0, 1.0, 0.0),
        float InYaw = -90.0f,
        float InPitch = 0.0f);
    
    /** Destructor */
    ~FFPSCameraController();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the controller
     * @param InCameraManager Camera manager to control (optional)
     */
    void Initialize(FCameraManager* InCameraManager = nullptr);
    
    /**
     * Reset camera to initial state
     */
    void Reset();
    
    // ========================================================================
    // Input Processing
    // ========================================================================
    
    /**
     * Process keyboard input for movement
     * @param Direction Movement direction
     * @param DeltaTime Time since last frame
     * @param bSprinting Whether sprint key is held
     */
    void ProcessKeyboard(ECameraMovement Direction, float DeltaTime, bool bSprinting = false);
    
    /**
     * Process mouse movement for look rotation
     * @param XOffset Mouse X offset (horizontal movement)
     * @param YOffset Mouse Y offset (vertical movement)
     * @param bConstrainPitch Whether to constrain pitch (override settings)
     */
    void ProcessMouseMovement(float XOffset, float YOffset, bool bConstrainPitch = true);
    
    /**
     * Process mouse scroll for zoom
     * @param YOffset Scroll offset (positive = zoom in)
     */
    void ProcessMouseScroll(float YOffset);
    
    // ========================================================================
    // Update
    // ========================================================================
    
    /**
     * Update camera vectors and apply to camera manager
     * Should be called each frame after processing input
     * @param DeltaTime Time since last frame
     */
    void Update(float DeltaTime);
    
    // ========================================================================
    // View Matrix
    // ========================================================================
    
    /**
     * Get the view matrix calculated from camera vectors
     * @return View matrix for rendering
     */
    FMatrix GetViewMatrix() const;
    
    // ========================================================================
    // Getters
    // ========================================================================
    
    /** Get camera position */
    const FVector& GetPosition() const { return m_position; }
    
    /** Get camera front vector (direction camera is facing) */
    const FVector& GetFront() const { return m_front; }
    
    /** Get camera up vector */
    const FVector& GetUp() const { return m_up; }
    
    /** Get camera right vector */
    const FVector& GetRight() const { return m_right; }
    
    /** Get world up vector */
    const FVector& GetWorldUp() const { return m_worldUp; }
    
    /** Get yaw angle in degrees */
    float GetYaw() const { return m_yaw; }
    
    /** Get pitch angle in degrees */
    float GetPitch() const { return m_pitch; }
    
    /** Get current FOV */
    float GetFOV() const { return m_fov; }
    
    /** Get camera settings */
    const FFPSCameraSettings& GetSettings() const { return m_settings; }
    
    /** Get mutable camera settings */
    FFPSCameraSettings& GetSettings() { return m_settings; }
    
    // ========================================================================
    // Setters
    // ========================================================================
    
    /**
     * Set camera position
     * @param InPosition New position
     */
    void SetPosition(const FVector& InPosition);
    
    /**
     * Set camera rotation (yaw and pitch)
     * @param InYaw New yaw angle in degrees
     * @param InPitch New pitch angle in degrees
     */
    void SetRotation(float InYaw, float InPitch);
    
    /**
     * Set FOV
     * @param InFOV New field of view in degrees
     */
    void SetFOV(float InFOV);
    
    /**
     * Set camera settings
     * @param InSettings New settings
     */
    void SetSettings(const FFPSCameraSettings& InSettings);
    
    /**
     * Set camera manager
     * @param InCameraManager Camera manager to control
     */
    void SetCameraManager(FCameraManager* InCameraManager);
    
    /**
     * Look at a target position
     * @param Target Target position to look at
     */
    void LookAt(const FVector& Target);
    
    // ========================================================================
    // State
    // ========================================================================
    
    /** Check if controller is enabled */
    bool IsEnabled() const { return m_bEnabled; }
    
    /** Enable/disable controller */
    void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }
    
    /** Check if first mouse input (for preventing jump on first frame) */
    bool IsFirstMouse() const { return m_bFirstMouse; }
    
    /** Reset first mouse flag */
    void ResetFirstMouse() { m_bFirstMouse = true; }

protected:
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * Update camera vectors from Euler angles
     * Called after yaw/pitch changes
     */
    void _updateCameraVectors();
    
    /**
     * Apply camera state to camera manager
     */
    void _applyCameraState();

protected:
    // ========================================================================
    // Camera Vectors
    // ========================================================================
    
    /** Camera position in world space */
    FVector m_position;
    
    /** Camera front direction (normalized) */
    FVector m_front;
    
    /** Camera up direction (normalized) */
    FVector m_up;
    
    /** Camera right direction (normalized) */
    FVector m_right;
    
    /** World up direction (typically Y-up or Z-up) */
    FVector m_worldUp;
    
    // ========================================================================
    // Euler Angles
    // ========================================================================
    
    /** Yaw angle in degrees (rotation around Y axis) */
    float m_yaw;
    
    /** Pitch angle in degrees (rotation around X axis) */
    float m_pitch;
    
    // ========================================================================
    // Camera Options
    // ========================================================================
    
    /** Field of view in degrees */
    float m_fov;
    
    /** Camera settings */
    FFPSCameraSettings m_settings;
    
    // ========================================================================
    // References
    // ========================================================================
    
    /** Camera manager to update (optional) */
    FCameraManager* m_cameraManager;
    
    // ========================================================================
    // State
    // ========================================================================
    
    /** Whether controller is enabled */
    bool m_bEnabled;
    
    /** First mouse input flag (prevents camera jump) */
    bool m_bFirstMouse;
    
    /** Last mouse X position */
    float m_lastMouseX;
    
    /** Last mouse Y position */
    float m_lastMouseY;
};

} // namespace MonsterEngine
