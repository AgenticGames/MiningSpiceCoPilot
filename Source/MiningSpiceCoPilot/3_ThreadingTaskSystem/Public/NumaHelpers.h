// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/PlatformProcess.h"

/**
 * Helper functions for NUMA (Non-Uniform Memory Access) operations.
 * Provides platform-independent wrappers for NUMA-related functionality.
 */
namespace NumaHelpers
{
    /**
     * Gets the number of cores per processor (NUMA node)
     * @return Number of cores per processor
     */
    MININGSPICECOPILOT_API uint32 GetNumberOfCoresPerProcessor();

    /**
     * Gets the processor mask for a specific NUMA domain
     * @param DomainId The NUMA domain ID
     * @return Processor mask representing the cores in the domain
     */
    MININGSPICECOPILOT_API uint64 GetProcessorMaskForDomain(uint32 DomainId);

    /**
     * Sets the processor affinity mask for the current thread
     * @param ProcessorMask Processor mask to set
     * @return True if successful
     */
    MININGSPICECOPILOT_API bool SetProcessorAffinityMask(uint64 ProcessorMask);

    /**
     * Gets the NUMA domain ID for a specific memory address
     * @param Address Memory address to check
     * @return NUMA domain ID where the memory is located
     */
    MININGSPICECOPILOT_API uint32 GetDomainForAddress(void* Address);

    /**
     * Allocates memory from a specific NUMA domain
     * @param Size Size of memory to allocate in bytes
     * @param DomainId NUMA domain ID to allocate from
     * @return Pointer to allocated memory, or nullptr if allocation failed
     */
    MININGSPICECOPILOT_API void* AllocateMemoryOnDomain(SIZE_T Size, uint32 DomainId);

    /**
     * Frees memory allocated with AllocateMemoryOnDomain
     * @param Ptr Pointer to memory to free
     */
    MININGSPICECOPILOT_API void FreeNUMAMemory(void* Ptr);
}; 