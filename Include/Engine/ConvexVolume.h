// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ConvexVolume.h
 * @brief Convex volume for frustum culling, inspired by UE5's FConvexVolume
 * 
 * This file defines the FConvexVolume class which represents a convex volume
 * defined by a set of planes. It is primarily used for view frustum culling
 * but can represent any convex volume for intersection tests.
 */

#include "Core/CoreMinimal.h"
#include "Math/MonsterMath.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
struct FBoxSphereBounds;

/**
 * Outcode for box-frustum intersection tests
 * Used to determine if a box is inside, outside, or intersecting a frustum
 */
struct FOutcode
{
    /** True if the box is completely inside the frustum */
    uint32 bInside : 1;
    
    /** True if the box is completely outside the frustum */
    uint32 bOutside : 1;

    FOutcode()
        : bInside(false)
        , bOutside(false)
    {
    }

    /** Returns true if the box is inside the frustum */
    FORCEINLINE bool GetInside() const { return bInside != 0; }
    
    /** Returns true if the box is outside the frustum */
    FORCEINLINE bool GetOutside() const { return bOutside != 0; }
    
    /** Sets the inside flag */
    FORCEINLINE void SetInside(bool bValue) { bInside = bValue ? 1 : 0; }
    
    /** Sets the outside flag */
    FORCEINLINE void SetOutside(bool bValue) { bOutside = bValue ? 1 : 0; }
};

/**
 * Represents a convex volume defined by a set of planes.
 * Used primarily for view frustum culling operations.
 * 
 * The planes are stored in two formats:
 * 1. Standard planes array for general use
 * 2. Permuted planes array for SIMD-optimized intersection tests
 */
class FConvexVolume
{
public:
    /** Array of planes defining the convex volume */
    TArray<FPlane> Planes;
    
    /** 
     * Permuted planes for SIMD-optimized intersection tests.
     * Planes are stored in SOA format: X0X1X2X3, Y0Y1Y2Y3, Z0Z1Z2Z3, W0W1W2W3
     * This allows for efficient 4-plane tests using SIMD instructions.
     */
    TArray<FPlane> PermutedPlanes;

public:
    /** Default constructor */
    FConvexVolume() = default;

    /** Destructor */
    ~FConvexVolume() = default;

    /**
     * Initializes the convex volume from an array of planes
     * @param InPlanes - Array of planes defining the volume
     */
    void Init(const TArray<FPlane>& InPlanes)
    {
        Planes = InPlanes;
        UpdatePermutedPlanes();
    }

    /**
     * Initializes the convex volume from a view-projection matrix
     * Extracts the 6 frustum planes from the matrix
     * @param ViewProjectionMatrix - Combined view-projection matrix
     * @param bUseNearPlane - Whether to include the near plane
     */
    void Init(const FMatrix& ViewProjectionMatrix, bool bUseNearPlane = true)
    {
        Planes.Empty(6);
        
        // Left plane
        FPlane LeftPlane;
        LeftPlane.X = ViewProjectionMatrix.M[0][3] + ViewProjectionMatrix.M[0][0];
        LeftPlane.Y = ViewProjectionMatrix.M[1][3] + ViewProjectionMatrix.M[1][0];
        LeftPlane.Z = ViewProjectionMatrix.M[2][3] + ViewProjectionMatrix.M[2][0];
        LeftPlane.W = ViewProjectionMatrix.M[3][3] + ViewProjectionMatrix.M[3][0];
        LeftPlane = LeftPlane.GetNormalized();
        Planes.Add(LeftPlane);

        // Right plane
        FPlane RightPlane;
        RightPlane.X = ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][0];
        RightPlane.Y = ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][0];
        RightPlane.Z = ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][0];
        RightPlane.W = ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][0];
        RightPlane = RightPlane.GetNormalized();
        Planes.Add(RightPlane);

        // Top plane
        FPlane TopPlane;
        TopPlane.X = ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][1];
        TopPlane.Y = ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][1];
        TopPlane.Z = ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][1];
        TopPlane.W = ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][1];
        TopPlane = TopPlane.GetNormalized();
        Planes.Add(TopPlane);

        // Bottom plane
        FPlane BottomPlane;
        BottomPlane.X = ViewProjectionMatrix.M[0][3] + ViewProjectionMatrix.M[0][1];
        BottomPlane.Y = ViewProjectionMatrix.M[1][3] + ViewProjectionMatrix.M[1][1];
        BottomPlane.Z = ViewProjectionMatrix.M[2][3] + ViewProjectionMatrix.M[2][1];
        BottomPlane.W = ViewProjectionMatrix.M[3][3] + ViewProjectionMatrix.M[3][1];
        BottomPlane = BottomPlane.GetNormalized();
        Planes.Add(BottomPlane);

        // Near plane (optional)
        if (bUseNearPlane)
        {
            FPlane NearPlane;
            NearPlane.X = ViewProjectionMatrix.M[0][2];
            NearPlane.Y = ViewProjectionMatrix.M[1][2];
            NearPlane.Z = ViewProjectionMatrix.M[2][2];
            NearPlane.W = ViewProjectionMatrix.M[3][2];
            NearPlane = NearPlane.GetNormalized();
            Planes.Add(NearPlane);
        }

        // Far plane
        FPlane FarPlane;
        FarPlane.X = ViewProjectionMatrix.M[0][3] - ViewProjectionMatrix.M[0][2];
        FarPlane.Y = ViewProjectionMatrix.M[1][3] - ViewProjectionMatrix.M[1][2];
        FarPlane.Z = ViewProjectionMatrix.M[2][3] - ViewProjectionMatrix.M[2][2];
        FarPlane.W = ViewProjectionMatrix.M[3][3] - ViewProjectionMatrix.M[3][2];
        FarPlane = FarPlane.GetNormalized();
        Planes.Add(FarPlane);

        UpdatePermutedPlanes();
    }

    /**
     * Tests if a point is inside the convex volume
     * @param Point - Point to test
     * @return true if the point is inside all planes
     */
    bool IntersectPoint(const FVector& Point) const
    {
        for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
        {
            const FPlane& Plane = Planes[PlaneIndex];
            float Distance = Plane.X * Point.X + Plane.Y * Point.Y + Plane.Z * Point.Z - Plane.W;
            if (Distance > 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Tests if a sphere intersects the convex volume
     * @param Origin - Center of the sphere
     * @param Radius - Radius of the sphere
     * @return true if the sphere intersects or is inside the volume
     */
    bool IntersectSphere(const FVector& Origin, float Radius) const
    {
        for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
        {
            const FPlane& Plane = Planes[PlaneIndex];
            float Distance = Plane.X * Origin.X + Plane.Y * Origin.Y + Plane.Z * Origin.Z - Plane.W;
            if (Distance > Radius)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Tests if an axis-aligned box intersects the convex volume
     * @param Origin - Center of the box
     * @param Extent - Half-extents of the box
     * @return true if the box intersects or is inside the volume
     */
    bool IntersectBox(const FVector& Origin, const FVector& Extent) const
    {
        for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
        {
            const FPlane& Plane = Planes[PlaneIndex];
            
            // Calculate the effective radius of the box along the plane normal
            float EffectiveRadius = 
                FMath::Abs(Plane.X * Extent.X) + 
                FMath::Abs(Plane.Y * Extent.Y) + 
                FMath::Abs(Plane.Z * Extent.Z);
            
            // Calculate distance from box center to plane
            float Distance = Plane.X * Origin.X + Plane.Y * Origin.Y + Plane.Z * Origin.Z - Plane.W;
            
            // If the box is completely outside this plane, it's outside the volume
            if (Distance > EffectiveRadius)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Tests if an axis-aligned box intersects the convex volume and returns outcode
     * @param Origin - Center of the box
     * @param Extent - Half-extents of the box
     * @return Outcode indicating inside/outside status
     */
    FOutcode GetBoxIntersectionOutcode(const FVector& Origin, const FVector& Extent) const
    {
        FOutcode Result;
        Result.SetInside(true);
        Result.SetOutside(false);

        for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
        {
            const FPlane& Plane = Planes[PlaneIndex];
            
            // Calculate the effective radius of the box along the plane normal
            float EffectiveRadius = 
                FMath::Abs(Plane.X * Extent.X) + 
                FMath::Abs(Plane.Y * Extent.Y) + 
                FMath::Abs(Plane.Z * Extent.Z);
            
            // Calculate distance from box center to plane
            float Distance = Plane.X * Origin.X + Plane.Y * Origin.Y + Plane.Z * Origin.Z - Plane.W;
            
            // If the box is completely outside this plane
            if (Distance > EffectiveRadius)
            {
                Result.SetInside(false);
                Result.SetOutside(true);
                return Result;
            }
            
            // If the box is not completely inside this plane
            if (Distance > -EffectiveRadius)
            {
                Result.SetInside(false);
            }
        }

        return Result;
    }

    /**
     * Tests if a box-sphere bounds intersects the convex volume
     * @param Bounds - Box-sphere bounds to test
     * @return true if the bounds intersects or is inside the volume
     */
    bool IntersectBoxSphereBounds(const FBoxSphereBounds& Bounds) const;

    /**
     * Gets the number of planes in the volume
     * @return Number of planes
     */
    FORCEINLINE int32 GetNumPlanes() const { return Planes.Num(); }

    /**
     * Gets a plane by index
     * @param Index - Plane index
     * @return Reference to the plane
     */
    FORCEINLINE const FPlane& GetPlane(int32 Index) const { return Planes[Index]; }

    /**
     * Checks if the permuted planes are valid for fast intersection tests
     * @return true if 8 permuted planes are available
     */
    FORCEINLINE bool HasPermutedPlanes() const { return PermutedPlanes.Num() == 8; }

protected:
    /**
     * Updates the permuted planes array for SIMD-optimized intersection tests
     * Pads to 8 planes if necessary (for 2 groups of 4 planes)
     */
    void UpdatePermutedPlanes()
    {
        // We need at least 4 planes for permuted format
        if (Planes.Num() < 4)
        {
            PermutedPlanes.Empty();
            return;
        }

        // Pad to 8 planes for two groups of 4
        int32 NumPaddedPlanes = ((Planes.Num() + 3) / 4) * 4;
        if (NumPaddedPlanes < 8)
        {
            NumPaddedPlanes = 8;
        }

        // Create padded planes array
        TArray<FPlane> PaddedPlanes;
        PaddedPlanes.SetNum(NumPaddedPlanes);
        
        for (int32 i = 0; i < NumPaddedPlanes; ++i)
        {
            if (i < Planes.Num())
            {
                PaddedPlanes[i] = Planes[i];
            }
            else
            {
                // Pad with a plane that never rejects (pointing away from everything)
                PaddedPlanes[i] = FPlane(0.0f, 0.0f, 0.0f, -1.0f);
            }
        }

        // Convert to permuted format (SOA)
        // For each group of 4 planes, store X0X1X2X3, Y0Y1Y2Y3, Z0Z1Z2Z3, W0W1W2W3
        PermutedPlanes.SetNum(8);
        
        // First group of 4 planes
        PermutedPlanes[0] = FPlane(PaddedPlanes[0].X, PaddedPlanes[1].X, PaddedPlanes[2].X, PaddedPlanes[3].X);
        PermutedPlanes[1] = FPlane(PaddedPlanes[0].Y, PaddedPlanes[1].Y, PaddedPlanes[2].Y, PaddedPlanes[3].Y);
        PermutedPlanes[2] = FPlane(PaddedPlanes[0].Z, PaddedPlanes[1].Z, PaddedPlanes[2].Z, PaddedPlanes[3].Z);
        PermutedPlanes[3] = FPlane(PaddedPlanes[0].W, PaddedPlanes[1].W, PaddedPlanes[2].W, PaddedPlanes[3].W);
        
        // Second group of 4 planes
        PermutedPlanes[4] = FPlane(PaddedPlanes[4].X, PaddedPlanes[5].X, PaddedPlanes[6].X, PaddedPlanes[7].X);
        PermutedPlanes[5] = FPlane(PaddedPlanes[4].Y, PaddedPlanes[5].Y, PaddedPlanes[6].Y, PaddedPlanes[7].Y);
        PermutedPlanes[6] = FPlane(PaddedPlanes[4].Z, PaddedPlanes[5].Z, PaddedPlanes[6].Z, PaddedPlanes[7].Z);
        PermutedPlanes[7] = FPlane(PaddedPlanes[4].W, PaddedPlanes[5].W, PaddedPlanes[6].W, PaddedPlanes[7].W);
    }
};

/**
 * View frustum - specialized convex volume for camera frustums
 * Provides additional functionality specific to view frustums
 */
class FViewFrustum : public FConvexVolume
{
public:
    /** Default constructor */
    FViewFrustum() = default;

    /**
     * Initializes the frustum from view and projection matrices
     * @param ViewMatrix - View matrix
     * @param ProjectionMatrix - Projection matrix
     */
    void Init(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix)
    {
        FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
        FConvexVolume::Init(ViewProjectionMatrix, true);
    }

    /**
     * Gets the frustum corners in world space
     * @param OutCorners - Array to receive the 8 frustum corners
     * @param InvViewProjectionMatrix - Inverse view-projection matrix
     */
    static void GetFrustumCorners(FVector OutCorners[8], const FMatrix& InvViewProjectionMatrix)
    {
        // NDC corners (clip space)
        static const FVector4 NDCCorners[8] = {
            FVector4(-1, -1, 0, 1), // Near bottom-left
            FVector4( 1, -1, 0, 1), // Near bottom-right
            FVector4( 1,  1, 0, 1), // Near top-right
            FVector4(-1,  1, 0, 1), // Near top-left
            FVector4(-1, -1, 1, 1), // Far bottom-left
            FVector4( 1, -1, 1, 1), // Far bottom-right
            FVector4( 1,  1, 1, 1), // Far top-right
            FVector4(-1,  1, 1, 1), // Far top-left
        };

        for (int32 i = 0; i < 8; ++i)
        {
            FVector4 WorldCorner = InvViewProjectionMatrix.TransformFVector4(NDCCorners[i]);
            OutCorners[i] = FVector(WorldCorner.X, WorldCorner.Y, WorldCorner.Z) / WorldCorner.W;
        }
    }
};

} // namespace MonsterEngine
