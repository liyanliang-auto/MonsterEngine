#pragma once

#include "Core/CoreMinimal.h"

/**
 * Forward declarations for Render Dependency Graph (RDG) types
 * Reference: UE5 RenderGraphFwd.h
 */

namespace MonsterRender {
namespace RDG {

// Forward declarations
class FRDGResource;
class FRDGTexture;
class FRDGBuffer;
class FRDGPass;
class FRDGBuilder;

// Resource references (raw pointers for lightweight handles)
using FRDGResourceRef = FRDGResource*;
using FRDGTextureRef = FRDGTexture*;
using FRDGBufferRef = FRDGBuffer*;
using FRDGPassRef = FRDGPass*;

// Enums (defined in RDGDefinitions.h)
enum class ERDGPassFlags : uint16;
enum class ERDGTextureFlags : uint8;
enum class ERDGBufferFlags : uint8;

}} // namespace MonsterRender::RDG
