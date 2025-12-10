// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Octree.h
 * @brief Octree spatial data structure for scene management
 * 
 * TOctree is a template class implementing an octree spatial data structure
 * for efficient spatial queries, culling, and collision detection.
 * Following UE5's TOctree2 architecture.
 */

#include "SceneTypes.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Math/Plane.h"
#include "Containers/Array.h"

#include <utility> // For std::move

namespace MonsterEngine
{

/**
 * Default octree semantics - can be specialized for different element types
 */
template<typename ElementType>
struct TOctreeDefaultSemantics
{
    /** Get the bounding box of an element */
    static FBox GetBoundingBox(const ElementType& Element)
    {
        return Element.GetBoundingBox();
    }

    /** Check if two elements are equal */
    static bool AreElementsEqual(const ElementType& A, const ElementType& B)
    {
        return A == B;
    }

    /** Set the octree ID on an element */
    static void SetElementId(ElementType& Element, uint32 Id)
    {
        Element.SetOctreeId(Id);
    }
};

/**
 * Octree node containing elements and child nodes
 */
template<typename ElementType, typename OctreeSemantics>
class TOctreeNode
{
public:
    /** Maximum number of elements per node before subdivision */
    static constexpr int32 MaxElementsPerNode = 16;

    /** Maximum depth of the octree */
    static constexpr int32 MaxDepth = 12;

    /** Number of children (8 for octree) */
    static constexpr int32 NumChildren = 8;

    TOctreeNode()
        : bIsLeaf(true)
        , Depth(0)
    {
        for (int32 i = 0; i < NumChildren; ++i)
        {
            Children[i] = nullptr;
        }
    }

    ~TOctreeNode()
    {
        // Delete children
        for (int32 i = 0; i < NumChildren; ++i)
        {
            if (Children[i])
            {
                delete Children[i];
                Children[i] = nullptr;
            }
        }
    }

    /** Check if this is a leaf node */
    bool IsLeaf() const { return bIsLeaf; }

    /** Get the elements in this node */
    const TArray<ElementType>& GetElements() const { return Elements; }

    /** Get the elements in this node (mutable) */
    TArray<ElementType>& GetElements() { return Elements; }

    /** Get a child node */
    TOctreeNode* GetChild(int32 Index) const { return Children[Index]; }

    /** Get the node bounds */
    const FBox& GetBounds() const { return Bounds; }

    /** Set the node bounds */
    void SetBounds(const FBox& InBounds) { Bounds = InBounds; }

    /** Get the node depth */
    int32 GetDepth() const { return Depth; }

    /** Set the node depth */
    void SetDepth(int32 InDepth) { Depth = InDepth; }

    /**
     * Get the child index for a point
     * @param Point The point to check
     * @param Center The center of this node
     * @return The child index (0-7)
     */
    static int32 GetChildIndex(const FVector& Point, const FVector& Center)
    {
        int32 Index = 0;
        if (Point.X >= Center.X) Index |= 1;
        if (Point.Y >= Center.Y) Index |= 2;
        if (Point.Z >= Center.Z) Index |= 4;
        return Index;
    }

    /**
     * Get the bounds of a child node
     * @param ChildIndex The child index (0-7)
     * @param ParentBounds The bounds of the parent node
     * @return The bounds of the child node
     */
    static FBox GetChildBounds(int32 ChildIndex, const FBox& ParentBounds)
    {
        FVector Center = ParentBounds.GetCenter();
        FVector Min = ParentBounds.Min;
        FVector Max = ParentBounds.Max;

        FVector ChildMin, ChildMax;

        ChildMin.X = (ChildIndex & 1) ? Center.X : Min.X;
        ChildMax.X = (ChildIndex & 1) ? Max.X : Center.X;

        ChildMin.Y = (ChildIndex & 2) ? Center.Y : Min.Y;
        ChildMax.Y = (ChildIndex & 2) ? Max.Y : Center.Y;

        ChildMin.Z = (ChildIndex & 4) ? Center.Z : Min.Z;
        ChildMax.Z = (ChildIndex & 4) ? Max.Z : Center.Z;

        return FBox(ChildMin, ChildMax);
    }

    /**
     * Subdivide this node into 8 children
     */
    void Subdivide()
    {
        if (!bIsLeaf || Depth >= MaxDepth)
        {
            return;
        }

        bIsLeaf = false;

        for (int32 i = 0; i < NumChildren; ++i)
        {
            Children[i] = new TOctreeNode();
            Children[i]->SetBounds(GetChildBounds(i, Bounds));
            Children[i]->SetDepth(Depth + 1);
        }

        // Redistribute elements to children
        TArray<ElementType> OldElements = std::move(Elements);
        for (ElementType& Element : OldElements)
        {
            FBox ElementBounds = OctreeSemantics::GetBoundingBox(Element);
            FVector ElementCenter = ElementBounds.GetCenter();
            FVector NodeCenter = Bounds.GetCenter();

            int32 ChildIndex = GetChildIndex(ElementCenter, NodeCenter);
            Children[ChildIndex]->AddElement(Element);
        }
    }

    /**
     * Add an element to this node
     * @param Element The element to add
     */
    void AddElement(ElementType& Element)
    {
        if (bIsLeaf)
        {
            Elements.Add(Element);

            // Subdivide if we have too many elements
            if (Elements.Num() > MaxElementsPerNode && Depth < MaxDepth)
            {
                Subdivide();
            }
        }
        else
        {
            // Find the appropriate child
            FBox ElementBounds = OctreeSemantics::GetBoundingBox(Element);
            FVector ElementCenter = ElementBounds.GetCenter();
            FVector NodeCenter = Bounds.GetCenter();

            int32 ChildIndex = GetChildIndex(ElementCenter, NodeCenter);
            Children[ChildIndex]->AddElement(Element);
        }
    }

    /**
     * Remove an element from this node
     * @param Element The element to remove
     * @return True if the element was found and removed
     */
    bool RemoveElement(const ElementType& Element)
    {
        if (bIsLeaf)
        {
            for (int32 i = 0; i < Elements.Num(); ++i)
            {
                if (OctreeSemantics::AreElementsEqual(Elements[i], Element))
                {
                    Elements.RemoveAt(i);
                    return true;
                }
            }
            return false;
        }
        else
        {
            // Search in children
            FBox ElementBounds = OctreeSemantics::GetBoundingBox(Element);
            FVector ElementCenter = ElementBounds.GetCenter();
            FVector NodeCenter = Bounds.GetCenter();

            int32 ChildIndex = GetChildIndex(ElementCenter, NodeCenter);
            return Children[ChildIndex]->RemoveElement(Element);
        }
    }

private:
    /** Elements stored in this node (only for leaf nodes) */
    TArray<ElementType> Elements;

    /** Child nodes (nullptr for leaf nodes) */
    TOctreeNode* Children[NumChildren];

    /** Bounds of this node */
    FBox Bounds;

    /** Whether this is a leaf node */
    bool bIsLeaf;

    /** Depth of this node in the tree */
    int32 Depth;
};

/**
 * Octree spatial data structure
 * 
 * An octree divides 3D space into 8 octants recursively for efficient
 * spatial queries. Used for visibility culling, collision detection,
 * and other spatial operations.
 * 
 * @tparam ElementType The type of elements stored in the octree
 * @tparam OctreeSemantics Semantics class defining how to get bounds, etc.
 */
template<typename ElementType, typename OctreeSemantics = TOctreeDefaultSemantics<ElementType>>
class TOctree
{
public:
    using NodeType = TOctreeNode<ElementType, OctreeSemantics>;

    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Constructor
     * @param InOrigin The center of the octree
     * @param InExtent The half-size of the octree in each dimension
     */
    TOctree(const FVector& InOrigin = FVector::ZeroVector, double InExtent = 1000000.0)
        : Origin(InOrigin)
        , Extent(InExtent)
        , NextElementId(1)
    {
        FVector HalfExtent(Extent, Extent, Extent);
        RootNode.SetBounds(FBox(Origin - HalfExtent, Origin + HalfExtent));
        RootNode.SetDepth(0);
    }

    /** Destructor */
    ~TOctree() = default;

    // ========================================================================
    // Element Management
    // ========================================================================

    /**
     * Add an element to the octree
     * @param Element The element to add
     * @return The ID assigned to the element
     */
    uint32 AddElement(ElementType& Element)
    {
        uint32 Id = NextElementId++;
        OctreeSemantics::SetElementId(Element, Id);
        RootNode.AddElement(Element);
        return Id;
    }

    /**
     * Remove an element from the octree
     * @param Element The element to remove
     * @return True if the element was found and removed
     */
    bool RemoveElement(const ElementType& Element)
    {
        return RootNode.RemoveElement(Element);
    }

    /**
     * Update an element's position in the octree
     * @param Element The element to update
     */
    void UpdateElement(ElementType& Element)
    {
        RemoveElement(Element);
        RootNode.AddElement(Element);
    }

    // ========================================================================
    // Spatial Queries
    // ========================================================================

    /**
     * Find all elements that intersect with a box
     * @param QueryBox The box to query
     * @param OutElements Output array of intersecting elements
     */
    void FindElementsInBox(const FBox& QueryBox, TArray<ElementType>& OutElements) const
    {
        FindElementsInBoxRecursive(&RootNode, QueryBox, OutElements);
    }

    /**
     * Find all elements that intersect with a sphere
     * @param Center The center of the sphere
     * @param Radius The radius of the sphere
     * @param OutElements Output array of intersecting elements
     */
    void FindElementsInSphere(const FVector& Center, double Radius, TArray<ElementType>& OutElements) const
    {
        FBox QueryBox = FBox::BuildAABB(Center, FVector(Radius, Radius, Radius));
        FindElementsInBoxRecursive(&RootNode, QueryBox, OutElements);

        // Filter by actual sphere intersection
        double RadiusSquared = Radius * Radius;
        for (int32 i = OutElements.Num() - 1; i >= 0; --i)
        {
            FBox ElementBounds = OctreeSemantics::GetBoundingBox(OutElements[i]);
            double DistSquared = ElementBounds.ComputeSquaredDistanceToPoint(Center);
            if (DistSquared > RadiusSquared)
            {
                OutElements.RemoveAt(i);
            }
        }
    }

    /**
     * Find all elements that intersect with a frustum
     * @param Planes The frustum planes (6 planes)
     * @param OutElements Output array of intersecting elements
     */
    void FindElementsInFrustum(const FPlane* Planes, int32 NumPlanes, TArray<ElementType>& OutElements) const
    {
        FindElementsInFrustumRecursive(&RootNode, Planes, NumPlanes, OutElements);
    }

    // ========================================================================
    // Iteration
    // ========================================================================

    /**
     * Iterate over all elements in the octree
     * @param Callback Function to call for each element
     */
    template<typename CallbackType>
    void ForEachElement(CallbackType Callback) const
    {
        ForEachElementRecursive(&RootNode, Callback);
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    /** Get the root node */
    const NodeType& GetRootNode() const { return RootNode; }

    /** Get the origin */
    const FVector& GetOrigin() const { return Origin; }

    /** Get the extent */
    double GetExtent() const { return Extent; }

private:
    // ========================================================================
    // Private Methods
    // ========================================================================

    void FindElementsInBoxRecursive(const NodeType* Node, const FBox& QueryBox, TArray<ElementType>& OutElements) const
    {
        if (!Node->GetBounds().Intersect(QueryBox))
        {
            return;
        }

        if (Node->IsLeaf())
        {
            for (const ElementType& Element : Node->GetElements())
            {
                FBox ElementBounds = OctreeSemantics::GetBoundingBox(Element);
                if (ElementBounds.Intersect(QueryBox))
                {
                    OutElements.Add(Element);
                }
            }
        }
        else
        {
            for (int32 i = 0; i < NodeType::NumChildren; ++i)
            {
                if (Node->GetChild(i))
                {
                    FindElementsInBoxRecursive(Node->GetChild(i), QueryBox, OutElements);
                }
            }
        }
    }

    void FindElementsInFrustumRecursive(const NodeType* Node, const FPlane* Planes, int32 NumPlanes, TArray<ElementType>& OutElements) const
    {
        // Check if node bounds are inside frustum
        if (!IsBoxInFrustum(Node->GetBounds(), Planes, NumPlanes))
        {
            return;
        }

        if (Node->IsLeaf())
        {
            for (const ElementType& Element : Node->GetElements())
            {
                FBox ElementBounds = OctreeSemantics::GetBoundingBox(Element);
                if (IsBoxInFrustum(ElementBounds, Planes, NumPlanes))
                {
                    OutElements.Add(Element);
                }
            }
        }
        else
        {
            for (int32 i = 0; i < NodeType::NumChildren; ++i)
            {
                if (Node->GetChild(i))
                {
                    FindElementsInFrustumRecursive(Node->GetChild(i), Planes, NumPlanes, OutElements);
                }
            }
        }
    }

    bool IsBoxInFrustum(const FBox& Box, const FPlane* Planes, int32 NumPlanes) const
    {
        FVector Center = Box.GetCenter();
        FVector Extent = Box.GetExtent();

        for (int32 i = 0; i < NumPlanes; ++i)
        {
            const FPlane& Plane = Planes[i];

            // Calculate the effective radius of the box for this plane
            double EffectiveRadius = 
                Extent.X * std::abs(Plane.X) +
                Extent.Y * std::abs(Plane.Y) +
                Extent.Z * std::abs(Plane.Z);

            // Calculate signed distance from center to plane
            double Distance = Plane.X * Center.X + Plane.Y * Center.Y + Plane.Z * Center.Z + Plane.W;

            // If the box is completely behind the plane, it's outside the frustum
            if (Distance < -EffectiveRadius)
            {
                return false;
            }
        }

        return true;
    }

    template<typename CallbackType>
    void ForEachElementRecursive(const NodeType* Node, CallbackType& Callback) const
    {
        if (Node->IsLeaf())
        {
            for (const ElementType& Element : Node->GetElements())
            {
                Callback(Element);
            }
        }
        else
        {
            for (int32 i = 0; i < NodeType::NumChildren; ++i)
            {
                if (Node->GetChild(i))
                {
                    ForEachElementRecursive(Node->GetChild(i), Callback);
                }
            }
        }
    }

private:
    /** Root node of the octree */
    NodeType RootNode;

    /** Origin of the octree */
    FVector Origin;

    /** Half-extent of the octree */
    double Extent;

    /** Next element ID to assign */
    uint32 NextElementId;
};

// ============================================================================
// Scene-Specific Octree Types
// ============================================================================

/**
 * Compact representation of a primitive for octree storage
 */
struct FPrimitiveSceneInfoCompact
{
    /** The primitive scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo;

    /** Cached bounds */
    FBoxSphereBounds Bounds;

    /** Octree ID */
    uint32 OctreeId;

    FPrimitiveSceneInfoCompact()
        : PrimitiveSceneInfo(nullptr)
        , OctreeId(0)
    {
    }

    /**
     * Constructor with primitive scene info
     * @param InPrimitiveSceneInfo The primitive scene info to store
     */
    explicit FPrimitiveSceneInfoCompact(FPrimitiveSceneInfo* InPrimitiveSceneInfo)
        : PrimitiveSceneInfo(InPrimitiveSceneInfo)
        , OctreeId(0)
    {
        // Bounds will be set separately after construction
    }

    FBox GetBoundingBox() const
    {
        return Bounds.GetBox();
    }

    void SetOctreeId(uint32 InId)
    {
        OctreeId = InId;
    }

    bool operator==(const FPrimitiveSceneInfoCompact& Other) const
    {
        return PrimitiveSceneInfo == Other.PrimitiveSceneInfo;
    }
};

/**
 * Semantics for primitive octree
 */
struct FPrimitiveOctreeSemantics
{
    static FBox GetBoundingBox(const FPrimitiveSceneInfoCompact& Element)
    {
        return Element.GetBoundingBox();
    }

    static bool AreElementsEqual(const FPrimitiveSceneInfoCompact& A, const FPrimitiveSceneInfoCompact& B)
    {
        return A.PrimitiveSceneInfo == B.PrimitiveSceneInfo;
    }

    static void SetElementId(FPrimitiveSceneInfoCompact& Element, uint32 Id)
    {
        Element.SetOctreeId(Id);
    }
};

/** Type alias for the scene primitive octree */
using FScenePrimitiveOctree = TOctree<FPrimitiveSceneInfoCompact, FPrimitiveOctreeSemantics>;

/**
 * Compact representation of a light for octree storage
 */
struct FLightSceneInfoCompactOctree
{
    /** The light scene info */
    FLightSceneInfo* LightSceneInfo;

    /** Cached bounds */
    FBoxSphereBounds Bounds;

    /** Octree ID */
    uint32 OctreeId;

    FLightSceneInfoCompactOctree()
        : LightSceneInfo(nullptr)
        , OctreeId(0)
    {
    }

    /**
     * Constructor with light scene info
     * @param InLightSceneInfo The light scene info to store
     */
    explicit FLightSceneInfoCompactOctree(FLightSceneInfo* InLightSceneInfo)
        : LightSceneInfo(InLightSceneInfo)
        , OctreeId(0)
    {
        // Bounds will be set separately after construction
    }

    FBox GetBoundingBox() const
    {
        return Bounds.GetBox();
    }

    void SetOctreeId(uint32 InId)
    {
        OctreeId = InId;
    }

    bool operator==(const FLightSceneInfoCompactOctree& Other) const
    {
        return LightSceneInfo == Other.LightSceneInfo;
    }
};

/**
 * Semantics for light octree
 */
struct FLightOctreeSemantics
{
    static FBox GetBoundingBox(const FLightSceneInfoCompactOctree& Element)
    {
        return Element.GetBoundingBox();
    }

    static bool AreElementsEqual(const FLightSceneInfoCompactOctree& A, const FLightSceneInfoCompactOctree& B)
    {
        return A.LightSceneInfo == B.LightSceneInfo;
    }

    static void SetElementId(FLightSceneInfoCompactOctree& Element, uint32 Id)
    {
        Element.SetOctreeId(Id);
    }
};

/** Type alias for the scene light octree */
using FSceneLightOctree = TOctree<FLightSceneInfoCompactOctree, FLightOctreeSemantics>;

} // namespace MonsterEngine
