// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Extensions to platform capabilities detection
 * Provides additional functionality for detecting CPU/GPU features
 */
class MININGSPICECOPILOT_API FPlatformMiscExtensions
{
public:
    /**
     * Checks if the current CPU supports SSE2 instructions
     * @return True if SSE2 is supported
     */
    static bool SupportsSSE2();
    
    /**
     * Checks if the current CPU supports AVX instructions
     * @return True if AVX is supported
     */
    static bool SupportsAVX();
    
    /**
     * Checks if the current CPU supports AVX2 instructions
     * @return True if AVX2 is supported
     */
    static bool SupportsAVX2();
    
    /**
     * Checks if the current GPU supports render targets
     * @return True if render targets are supported
     */
    static bool SupportsRenderTarget();
}; 