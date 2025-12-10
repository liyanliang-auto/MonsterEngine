// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ConvexVolume.cpp
 * @brief Implementation of FConvexVolume
 */

#include "Engine/ConvexVolume.h"
#include "Engine/SceneTypes.h"

namespace MonsterEngine
{

bool FConvexVolume::IntersectBoxSphereBounds(const FBoxSphereBounds& Bounds) const
{
    // First do a quick sphere test
    if (!IntersectSphere(Bounds.Origin, Bounds.SphereRadius))
    {
        return false;
    }
    
    // Then do the more accurate box test
    return IntersectBox(Bounds.Origin, Bounds.BoxExtent);
}

} // namespace MonsterEngine
