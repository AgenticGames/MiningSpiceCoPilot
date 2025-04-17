// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MemoryPoolTypes.generated.h"

/**
 * Memory usage patterns for optimizing allocation strategies in pools
 */
UENUM()
enum class EPoolMemoryUsage : uint8
{
    /** Sequential access optimized for streaming operations */
    Sequential,
    
    /** Interleaved access pattern for structured data */
    Interleaved,
    
    /** Tiled access pattern for 2D data structures */
    Tiled,
    
    /** General purpose with balanced characteristics */
    General,
    
    /** Concurrent access pattern optimized for multi-threading */
    Concurrent
}; 