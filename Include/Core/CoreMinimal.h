#pragma once

/**
 * CoreMinimal.h - Minimal core includes for MonsterEngine
 * 
 * This file now delegates to the PCH header for faster compilation.
 * All commonly used headers are in MonsterEnginePCH.h
 */

// Include PCH header which contains all common includes
#include "Core/MonsterEnginePCH.h"

// Engine namespace
namespace MonsterRender {

// Import MonsterEngine smart pointers into MonsterRender namespace
using MonsterEngine::TSharedPtr;
using MonsterEngine::TSharedRef;
using MonsterEngine::TWeakPtr;
using MonsterEngine::TUniquePtr;
using MonsterEngine::MakeShared;
using MonsterEngine::MakeSharedPooled;
using MonsterEngine::MakeUnique;
using MonsterEngine::MakeShareable;
using MonsterEngine::StaticCastSharedPtr;
using MonsterEngine::StaticCastSharedRef;
using MonsterEngine::ConstCastSharedPtr;
using MonsterEngine::ConstCastSharedRef;
    // Forward declarations
    class Engine;
    class Renderer;
    
    namespace RHI {
        class IRHIDevice;
        class IRHICommandList;
        class IRHIResource;
        class IRHIBuffer;
        class IRHITexture;
        class IRHIPipelineState;
    }
}
