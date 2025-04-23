// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMemoryAwareService.generated.h"

/**
 * Interface for services that can report their memory usage
 * Used by the service manager for memory tracking and optimization
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMemoryAwareService : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for services that can report their memory usage
 * Used by the service manager for memory tracking and optimization
 */
class MININGSPICECOPILOT_API IMemoryAwareService
{
    GENERATED_BODY()

public:
    /**
     * Get the current memory usage of the service in bytes
     * @return Memory usage in bytes
     */
    virtual uint64 GetMemoryUsage() const = 0;
    
    /**
     * Trim memory usage if possible
     * @param TargetUsageBytes Desired memory usage in bytes
     * @return True if memory was trimmed, false if not possible
     */
    virtual bool TrimMemory(uint64 TargetUsageBytes) = 0;
}; 