// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SharedPointerFwd.h
 * @brief Forward declarations for smart pointer types
 * 
 * This file provides forward declarations and the ESPMode enum for the
 * MonsterEngine smart pointer system. Based on UE5's SharedPointerFwd.h.
 * 
 * The smart pointer system provides:
 * - TSharedRef: Non-nullable reference-counted smart pointer
 * - TSharedPtr: Nullable reference-counted smart pointer  
 * - TWeakPtr: Weak reference that doesn't prevent destruction
 * - TSharedFromThis: Mixin to get shared pointer from 'this'
 */

#include "Core/CoreTypes.h"

namespace MonsterEngine
{

/**
 * ESPMode is used to select between 'fast' or 'thread safe' shared pointer types.
 * This is only used by templates at compile time to generate one code path or another.
 */
enum class ESPMode : uint8
{
    /** Forced to be not thread-safe. Faster but not safe for multi-threaded access. */
    NotThreadSafe = 0,

    /** Thread-safe using atomic operations. Slower but safe for multi-threaded access. */
    ThreadSafe = 1
};

// Forward declarations
// By default, thread safety features are turned on (Mode = ESPMode::ThreadSafe).
// If you are more concerned with performance of ref-counting, use ESPMode::NotThreadSafe.

template<class ObjectType, ESPMode Mode = ESPMode::ThreadSafe> class TSharedRef;
template<class ObjectType, ESPMode Mode = ESPMode::ThreadSafe> class TSharedPtr;
template<class ObjectType, ESPMode Mode = ESPMode::ThreadSafe> class TWeakPtr;
template<class ObjectType, ESPMode Mode = ESPMode::ThreadSafe> class TSharedFromThis;

// Type aliases for convenience
template<class ObjectType> using TSharedRefTS = TSharedRef<ObjectType, ESPMode::ThreadSafe>;
template<class ObjectType> using TSharedPtrTS = TSharedPtr<ObjectType, ESPMode::ThreadSafe>;
template<class ObjectType> using TWeakPtrTS = TWeakPtr<ObjectType, ESPMode::ThreadSafe>;

template<class ObjectType> using TSharedRefNTS = TSharedRef<ObjectType, ESPMode::NotThreadSafe>;
template<class ObjectType> using TSharedPtrNTS = TSharedPtr<ObjectType, ESPMode::NotThreadSafe>;
template<class ObjectType> using TWeakPtrNTS = TWeakPtr<ObjectType, ESPMode::NotThreadSafe>;

} // namespace MonsterEngine
