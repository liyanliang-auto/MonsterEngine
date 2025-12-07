// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Containers.h
 * @brief Master include file for all container types
 * 
 * This file includes all container headers for convenience.
 * Include this file to get access to all container types.
 */

// Forward declarations
#include "ContainerFwd.h"

// Allocation policies
#include "ContainerAllocationPolicies.h"

// Core containers
#include "Array.h"
#include "BitArray.h"
#include "SparseArray.h"
#include "Set.h"
#include "Map.h"

// String types
#include "String.h"
#include "Name.h"
#include "Text.h"

// Queue containers
#include "Queue.h"
#include "Deque.h"
#include "CircularBuffer.h"

// Script containers (for reflection)
#include "ScriptArray.h"

// Serialization
#include "Serialization/Archive.h"
