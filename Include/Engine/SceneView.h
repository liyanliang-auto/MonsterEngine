// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneView.h
 * @brief Scene view classes for rendering, inspired by UE5's FSceneView
 * 
 * This file defines the core view classes used for rendering:
 * - FViewMatrices: All matrices needed for view transformations
 * - FSceneViewInitOptions: Options for initializing a scene view
 * - FSceneView: Represents a single view into the scene
 * - FSceneViewFamily: A collection of views rendered together
 */

#include "Core/CoreMinimal.h"
#include "Core/Color.h"
#include "Math/MonsterMath.h"
#include "Engine/SceneTypes.h"
#include "Engine/ConvexVolume.h"
#include "Containers/Array.h"
#include "Containers/Set.h"

namespace MonsterEngine
{

// Forward declarations
class FSceneInterface;
class FSceneView;
class FSceneViewFamily;
class FRenderTarget;

/**
 * Integer rectangle for viewport definitions
 */
struct FIntRect
{
    FIntPoint Min;
    FIntPoint Max;

    FIntRect() : Min(0, 0), Max(0, 0) {}
    FIntRect(int32 X0, int32 Y0, int32 X1, int32 Y1) : Min(X0, Y0), Max(X1, Y1) {}
    FIntRect(const FIntPoint& InMin, const FIntPoint& InMax) : Min(InMin), Max(InMax) {}

    FORCEINLINE int32 Width() const { return Max.X - Min.X; }
    FORCEINLINE int32 Height() const { return Max.Y - Min.Y; }
    FORCEINLINE FIntPoint Size() const { return FIntPoint(Width(), Height()); }
    FORCEINLINE int32 Area() const { return Width() * Height(); }
    
    FORCEINLINE bool IsValid() const { return Min.X < Max.X && Min.Y < Max.Y; }
    
    FORCEINLINE bool Contains(const FIntPoint& Point) const
    {
        return Point.X >= Min.X && Point.X < Max.X && Point.Y >= Min.Y && Point.Y < Max.Y;
    }

    FORCEINLINE bool operator==(const FIntRect& Other) const
    {
        return Min == Other.Min && Max == Other.Max;
    }

    FORCEINLINE bool operator!=(const FIntRect& Other) const
    {
        return !(*this == Other);
    }
};

/**
 * Stereoscopic pass type
 */
enum class EStereoscopicPass : uint8
{
    /** Full screen pass (non-stereo) */
    eSSP_FULL,
    /** Primary eye pass */
    eSSP_PRIMARY,
    /** Secondary eye pass */
    eSSP_SECONDARY
};

/**
 * Projection data for a scene view
 */
struct FSceneViewProjectionData
{
    /** The view origin in world space */
    FVector ViewOrigin;

    /** Rotation matrix transforming from world space to view space */
    FMatrix ViewRotationMatrix;

    /** Projection matrix (clip space Z=1 is near plane, Z=0 is far plane) */
    FMatrix ProjectionMatrix;

protected:
    /** The unconstrained view rectangle */
    FIntRect ViewRect;

    /** The constrained view rectangle (may differ due to aspect ratio) */
    FIntRect ConstrainedViewRect;

public:
    /** Sets the view rectangle */
    void SetViewRectangle(const FIntRect& InViewRect)
    {
        ViewRect = InViewRect;
        ConstrainedViewRect = InViewRect;
    }

    /** Sets the constrained view rectangle */
    void SetConstrainedViewRectangle(const FIntRect& InViewRect)
    {
        ConstrainedViewRect = InViewRect;
    }

    /** Checks if the view rectangle is valid */
    bool IsValidViewRectangle() const
    {
        return (ConstrainedViewRect.Min.X >= 0) &&
               (ConstrainedViewRect.Min.Y >= 0) &&
               (ConstrainedViewRect.Width() > 0) &&
               (ConstrainedViewRect.Height() > 0);
    }

    /** Returns true if using perspective projection */
    bool IsPerspectiveProjection() const
    {
        return ProjectionMatrix.M[3][3] < 1.0f;
    }

    /** Gets the unconstrained view rectangle */
    const FIntRect& GetViewRect() const { return ViewRect; }
    
    /** Gets the constrained view rectangle */
    const FIntRect& GetConstrainedViewRect() const { return ConstrainedViewRect; }

    /** Computes the combined view-projection matrix */
    FMatrix ComputeViewProjectionMatrix() const
    {
        return FMatrix::MakeTranslation(-ViewOrigin) * ViewRotationMatrix * ProjectionMatrix;
    }
};

/**
 * All matrices needed for view transformations
 * Cached for efficiency and to avoid recomputation
 */
struct FViewMatrices
{
public:
    FViewMatrices()
    {
        ProjectionMatrix.SetIdentity();
        InvProjectionMatrix.SetIdentity();
        ViewMatrix.SetIdentity();
        InvViewMatrix.SetIdentity();
        ViewProjectionMatrix.SetIdentity();
        InvViewProjectionMatrix.SetIdentity();
        TranslatedViewMatrix.SetIdentity();
        InvTranslatedViewMatrix.SetIdentity();
        TranslatedViewProjectionMatrix.SetIdentity();
        InvTranslatedViewProjectionMatrix.SetIdentity();
        PreViewTranslation = FVector::ZeroVector;
        ViewOrigin = FVector::ZeroVector;
        ProjectionScale = FVector2D::ZeroVector;
        ScreenScale = 1.0f;
    }

    /**
     * Initializes view matrices from projection data
     * @param ProjectionData - View projection data
     */
    void Init(const FSceneViewProjectionData& ProjectionData)
    {
        ViewOrigin = ProjectionData.ViewOrigin;
        ProjectionMatrix = ProjectionData.ProjectionMatrix;
        
        // Compute view matrix from rotation and translation
        ViewMatrix = FMatrix::MakeTranslation(-ViewOrigin) * ProjectionData.ViewRotationMatrix;
        
        // Compute inverse matrices
        InvProjectionMatrix = ProjectionMatrix.Inverse();
        InvViewMatrix = ViewMatrix.Inverse();
        
        // Compute combined matrices
        ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
        InvViewProjectionMatrix = InvProjectionMatrix * InvViewMatrix;
        
        // Compute translated view matrices (for precision)
        PreViewTranslation = -ViewOrigin;
        TranslatedViewMatrix = ProjectionData.ViewRotationMatrix;
        InvTranslatedViewMatrix = TranslatedViewMatrix.Inverse();
        TranslatedViewProjectionMatrix = TranslatedViewMatrix * ProjectionMatrix;
        InvTranslatedViewProjectionMatrix = InvProjectionMatrix * InvTranslatedViewMatrix;
        
        // Compute projection scale
        const FIntRect& ViewRect = ProjectionData.GetConstrainedViewRect();
        ProjectionScale.X = 0.5f * ViewRect.Width() * ProjectionMatrix.M[0][0];
        ProjectionScale.Y = 0.5f * ViewRect.Height() * ProjectionMatrix.M[1][1];
        
        // Compute screen scale for LOD calculations
        ScreenScale = FMath::Max(ProjectionScale.X, ProjectionScale.Y);
    }

    /** Updates the view matrix for a new view location and rotation */
    void UpdateViewMatrix(const FVector& ViewLocation, const FRotator& ViewRotation)
    {
        ViewOrigin = ViewLocation;
        
        // Create rotation matrix from rotator
        FMatrix RotationMatrix = FMatrix::MakeFromRotator(ViewRotation);
        
        // Update view matrix
        ViewMatrix = FMatrix::MakeTranslation(-ViewOrigin) * RotationMatrix;
        InvViewMatrix = ViewMatrix.Inverse();
        
        // Update combined matrices
        ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
        InvViewProjectionMatrix = InvProjectionMatrix * InvViewMatrix;
        
        // Update translated matrices
        PreViewTranslation = -ViewOrigin;
        TranslatedViewMatrix = RotationMatrix;
        InvTranslatedViewMatrix = TranslatedViewMatrix.Inverse();
        TranslatedViewProjectionMatrix = TranslatedViewMatrix * ProjectionMatrix;
        InvTranslatedViewProjectionMatrix = InvProjectionMatrix * InvTranslatedViewMatrix;
    }

    // Accessors
    FORCEINLINE const FMatrix& GetProjectionMatrix() const { return ProjectionMatrix; }
    FORCEINLINE const FMatrix& GetInvProjectionMatrix() const { return InvProjectionMatrix; }
    FORCEINLINE const FMatrix& GetViewMatrix() const { return ViewMatrix; }
    FORCEINLINE const FMatrix& GetInvViewMatrix() const { return InvViewMatrix; }
    FORCEINLINE const FMatrix& GetViewProjectionMatrix() const { return ViewProjectionMatrix; }
    FORCEINLINE const FMatrix& GetInvViewProjectionMatrix() const { return InvViewProjectionMatrix; }
    FORCEINLINE const FMatrix& GetTranslatedViewMatrix() const { return TranslatedViewMatrix; }
    FORCEINLINE const FMatrix& GetInvTranslatedViewMatrix() const { return InvTranslatedViewMatrix; }
    FORCEINLINE const FMatrix& GetTranslatedViewProjectionMatrix() const { return TranslatedViewProjectionMatrix; }
    FORCEINLINE const FMatrix& GetInvTranslatedViewProjectionMatrix() const { return InvTranslatedViewProjectionMatrix; }
    FORCEINLINE const FVector& GetPreViewTranslation() const { return PreViewTranslation; }
    FORCEINLINE const FVector& GetViewOrigin() const { return ViewOrigin; }
    FORCEINLINE float GetScreenScale() const { return ScreenScale; }
    FORCEINLINE const FVector2D& GetProjectionScale() const { return ProjectionScale; }
    
    /** Returns true if using perspective projection */
    FORCEINLINE bool IsPerspectiveProjection() const
    {
        return ProjectionMatrix.M[3][3] < 1.0f;
    }

    /** Computes the near plane distance */
    float ComputeNearPlane() const
    {
        return (ProjectionMatrix.M[3][3] - ProjectionMatrix.M[3][2]) / 
               (ProjectionMatrix.M[2][2] - ProjectionMatrix.M[2][3]);
    }

private:
    /** Projection matrix (view to clip) */
    FMatrix ProjectionMatrix;
    
    /** Inverse projection matrix (clip to view) */
    FMatrix InvProjectionMatrix;
    
    /** View matrix (world to view) */
    FMatrix ViewMatrix;
    
    /** Inverse view matrix (view to world) */
    FMatrix InvViewMatrix;
    
    /** Combined view-projection matrix (world to clip) */
    FMatrix ViewProjectionMatrix;
    
    /** Inverse view-projection matrix (clip to world) */
    FMatrix InvViewProjectionMatrix;
    
    /** Translated view matrix (for precision, origin at view position) */
    FMatrix TranslatedViewMatrix;
    
    /** Inverse translated view matrix */
    FMatrix InvTranslatedViewMatrix;
    
    /** Translated view-projection matrix */
    FMatrix TranslatedViewProjectionMatrix;
    
    /** Inverse translated view-projection matrix */
    FMatrix InvTranslatedViewProjectionMatrix;
    
    /** Translation to apply before TranslatedViewProjectionMatrix */
    FVector PreViewTranslation;
    
    /** View origin in world space */
    FVector ViewOrigin;
    
    /** Scale applied by projection in X and Y */
    FVector2D ProjectionScale;
    
    /** Screen scale for LOD calculations */
    float ScreenScale;
};

/**
 * Options for initializing a scene view
 */
struct FSceneViewInitOptions : public FSceneViewProjectionData
{
    /** The view family this view belongs to */
    const FSceneViewFamily* ViewFamily = nullptr;

    /** Player index for this view (-1 if not a player view) */
    int32 PlayerIndex = INDEX_NONE;

    /** Background color for the view */
    FLinearColor BackgroundColor = FLinearColor::Transparent;

    /** Overlay color applied to the view */
    FLinearColor OverlayColor = FLinearColor::Transparent;

    /** Color scale applied to the view */
    FLinearColor ColorScale = FLinearColor::White;

    /** Stereo pass type */
    EStereoscopicPass StereoPass = EStereoscopicPass::eSSP_FULL;

    /** Stereo view index for multi-view rendering */
    int32 StereoViewIndex = INDEX_NONE;

    /** Conversion from world units to meters */
    float WorldToMetersScale = 100.0f;

    /** View location without stereo offsets */
    FVector ViewLocation = FVector::ZeroVector;

    /** View rotation without stereo offsets */
    FRotator ViewRotation = FRotator::ZeroRotator;

    /** Set of primitives to hide */
    TSet<FPrimitiveComponentId> HiddenPrimitives;

    /** Cursor position in viewport coordinates (-1,-1 if not set) */
    FIntPoint CursorPos = FIntPoint(-1, -1);

    /** LOD distance factor (1.0 = normal) */
    float LODDistanceFactor = 1.0f;

    /** Override far clipping plane distance (-1 = use default) */
    float OverrideFarClippingPlaneDistance = -1.0f;

    /** Field of view in degrees */
    float FOV = 90.0f;

    /** Desired field of view (before any modifications) */
    float DesiredFOV = 90.0f;

    /** Near clip plane distance */
    float NearClipPlane = 10.0f;

    /** Far clip plane distance */
    float FarClipPlane = 100000.0f;

    /** Was there a camera cut this frame? */
    bool bInCameraCut = false;

    /** Whether to use FOV when computing mesh LOD */
    bool bUseFieldOfViewForLOD = true;

    /** Whether this view is for scene capture */
    bool bIsSceneCapture = false;

    /** Whether this view is for reflection capture */
    bool bIsReflectionCapture = false;

    /** Whether this view is for planar reflection */
    bool bIsPlanarReflection = false;

    FSceneViewInitOptions() = default;
};

/**
 * Represents a single view into the scene
 * Contains all information needed to render from a specific viewpoint
 */
class FSceneView
{
public:
    /** The view family this view belongs to */
    const FSceneViewFamily* Family;

    /** All view transformation matrices */
    FViewMatrices ViewMatrices;

    /** Previous frame's view matrices (for motion blur, TAA) */
    FViewMatrices PrevViewMatrices;

    /** View frustum for culling */
    FViewFrustum ViewFrustum;

    /** Unconstrained view rectangle */
    FIntRect UnscaledViewRect;

    /** Constrained view rectangle */
    FIntRect ViewRect;

    /** View origin in world space */
    FVector ViewLocation;

    /** View rotation */
    FRotator ViewRotation;

    /** Background color */
    FLinearColor BackgroundColor;

    /** Overlay color */
    FLinearColor OverlayColor;

    /** Color scale */
    FLinearColor ColorScale;

    /** Stereo pass type */
    EStereoscopicPass StereoPass;

    /** Stereo view index */
    int32 StereoViewIndex;

    /** Player index */
    int32 PlayerIndex;

    /** Field of view in degrees */
    float FOV;

    /** Near clip plane distance */
    float NearClipPlane;

    /** Far clip plane distance */
    float FarClipPlane;

    /** LOD distance factor */
    float LODDistanceFactor;

    /** World to meters scale */
    float WorldToMetersScale;

    /** Set of hidden primitives */
    TSet<FPrimitiveComponentId> HiddenPrimitives;

    /** Cursor position */
    FIntPoint CursorPos;

    /** Was there a camera cut this frame? */
    bool bInCameraCut;

    /** Whether this is a scene capture view */
    bool bIsSceneCapture;

    /** Whether this is a reflection capture view */
    bool bIsReflectionCapture;

    /** Whether this is a planar reflection view */
    bool bIsPlanarReflection;

    /** Whether to use FOV for LOD calculations */
    bool bUseFieldOfViewForLOD;

public:
    /** Constructor from init options */
    FSceneView(const FSceneViewInitOptions& InitOptions);

    /** Destructor */
    virtual ~FSceneView() = default;

    // ============================================================================
    // Coordinate Transformations
    // ============================================================================

    /** Transforms a point from world space to screen space */
    FVector4 WorldToScreen(const FVector& WorldPoint) const;

    /** Transforms a point from screen space to world space */
    FVector ScreenToWorld(const FVector4& ScreenPoint) const;

    /** Transforms a point from screen space to pixel coordinates */
    bool ScreenToPixel(const FVector4& ScreenPoint, FVector2D& OutPixelLocation) const;

    /** Transforms a point from pixel coordinates to screen space */
    FVector4 PixelToScreen(float X, float Y, float Z) const;

    /** Transforms a point from world space to pixel coordinates */
    bool WorldToPixel(const FVector& WorldPoint, FVector2D& OutPixelLocation) const;

    /** Transforms a point from pixel coordinates to world space */
    FVector4 PixelToWorld(float X, float Y, float Z) const;

    /** Projects a world point to screen space (with perspective divide) */
    FPlane Project(const FVector& WorldPoint) const;

    /** Deprojects a screen point to world space */
    FVector Deproject(const FPlane& ScreenPoint) const;

    /**
     * Deprojects 2D screen coordinates to a 3D world ray
     * @param ScreenPos - Screen coordinates in pixels
     * @param OutWorldOrigin - Ray origin in world space
     * @param OutWorldDirection - Ray direction in world space
     */
    void DeprojectScreenToWorld(const FVector2D& ScreenPos, FVector& OutWorldOrigin, FVector& OutWorldDirection) const;

    // ============================================================================
    // View Accessors
    // ============================================================================

    /** Gets the view right vector */
    FORCEINLINE FVector GetViewRight() const { return ViewMatrices.GetViewMatrix().GetAxisX(); }
    
    /** Gets the view up vector */
    FORCEINLINE FVector GetViewUp() const { return ViewMatrices.GetViewMatrix().GetAxisY(); }
    
    /** Gets the view forward vector */
    FORCEINLINE FVector GetViewDirection() const { return ViewMatrices.GetViewMatrix().GetAxisZ(); }

    /** Returns true if using perspective projection */
    FORCEINLINE bool IsPerspectiveProjection() const { return ViewMatrices.IsPerspectiveProjection(); }

    /** Gets the view origin for LOD calculations */
    FVector GetLODOrigin() const { return ViewLocation; }

    /** Gets the aspect ratio of the view */
    float GetAspectRatio() const
    {
        return ViewRect.Width() > 0 ? static_cast<float>(ViewRect.Width()) / ViewRect.Height() : 1.0f;
    }

protected:
    /** Sets up the view frustum from the view matrices */
    void SetupViewFrustum();
};

/**
 * Time information for a view family
 */
struct FGameTime
{
    /** Real time in seconds since application start */
    float RealTimeSeconds = 0.0f;

    /** World time in seconds (may be dilated) */
    float WorldTimeSeconds = 0.0f;

    /** Delta time for this frame in seconds */
    float DeltaWorldTimeSeconds = 0.0f;

    /** Delta real time for this frame */
    float DeltaRealTimeSeconds = 0.0f;

    /** Creates a game time with the specified values */
    static FGameTime Create(float InRealTime, float InWorldTime, float InDeltaWorld, float InDeltaReal)
    {
        FGameTime Time;
        Time.RealTimeSeconds = InRealTime;
        Time.WorldTimeSeconds = InWorldTime;
        Time.DeltaWorldTimeSeconds = InDeltaWorld;
        Time.DeltaRealTimeSeconds = InDeltaReal;
        return Time;
    }

    /** Gets the real time in seconds */
    float GetRealTimeSeconds() const { return RealTimeSeconds; }
    
    /** Gets the world time in seconds */
    float GetWorldTimeSeconds() const { return WorldTimeSeconds; }
};

/**
 * A collection of views rendered together
 * All views in a family share the same render target and scene
 */
class FSceneViewFamily
{
public:
    /** Construction parameters */
    struct ConstructionValues
    {
        /** The render target for the views */
        FRenderTarget* RenderTarget = nullptr;

        /** The scene being rendered */
        FSceneInterface* Scene = nullptr;

        /** Time information */
        FGameTime Time;

        /** Gamma correction value */
        float GammaCorrection = 1.0f;

        /** Whether the view family is updated in real-time */
        bool bRealtimeUpdate = true;

        /** Whether to defer clearing the render target */
        bool bDeferClear = false;

        /** Whether to resolve the scene to the render target */
        bool bResolveScene = true;

        ConstructionValues() = default;

        ConstructionValues(FRenderTarget* InRenderTarget, FSceneInterface* InScene)
            : RenderTarget(InRenderTarget)
            , Scene(InScene)
        {
        }

        ConstructionValues& SetTime(const FGameTime& InTime)
        {
            Time = InTime;
            return *this;
        }

        ConstructionValues& SetGammaCorrection(float InGamma)
        {
            GammaCorrection = InGamma;
            return *this;
        }

        ConstructionValues& SetRealtimeUpdate(bool bInRealtimeUpdate)
        {
            bRealtimeUpdate = bInRealtimeUpdate;
            return *this;
        }

        ConstructionValues& SetDeferClear(bool bInDeferClear)
        {
            bDeferClear = bInDeferClear;
            return *this;
        }

        ConstructionValues& SetResolveScene(bool bInResolveScene)
        {
            bResolveScene = bInResolveScene;
            return *this;
        }
    };

public:
    /** The views in this family */
    TArray<const FSceneView*> Views;

    /** The render target */
    FRenderTarget* RenderTarget;

    /** The scene being rendered */
    FSceneInterface* Scene;

    /** Time information */
    FGameTime Time;

    /** Frame number */
    uint32 FrameNumber;

    /** Gamma correction value */
    float GammaCorrection;

    /** Whether the view family is updated in real-time */
    bool bRealtimeUpdate;

    /** Whether to defer clearing the render target */
    bool bDeferClear;

    /** Whether to resolve the scene to the render target */
    bool bResolveScene;

    /** Whether the world is paused */
    bool bWorldIsPaused;

public:
    /** Constructor */
    FSceneViewFamily(const ConstructionValues& CVS);

    /** Destructor */
    virtual ~FSceneViewFamily();

    /** Gets the number of views */
    FORCEINLINE int32 GetNumViews() const { return Views.Num(); }

    /** Gets a view by index */
    FORCEINLINE const FSceneView* GetView(int32 Index) const { return Views[Index]; }

    /** Gets the primary view (first view) */
    FORCEINLINE const FSceneView* GetPrimaryView() const { return Views.Num() > 0 ? Views[0] : nullptr; }

    /** Adds a view to the family */
    void AddView(const FSceneView* View)
    {
        Views.Add(View);
    }

private:
    // Prevent copying
    FSceneViewFamily(const FSceneViewFamily&) = delete;
    FSceneViewFamily& operator=(const FSceneViewFamily&) = delete;
};

/**
 * A view family that owns and deletes its views
 */
class FSceneViewFamilyContext : public FSceneViewFamily
{
public:
    FSceneViewFamilyContext(const ConstructionValues& CVS)
        : FSceneViewFamily(CVS)
    {
    }

    virtual ~FSceneViewFamilyContext();
};

} // namespace MonsterEngine
