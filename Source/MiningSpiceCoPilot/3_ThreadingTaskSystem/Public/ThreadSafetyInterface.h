// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Interface for thread safety functionality
 * Provides common thread safety methods and utilities
 */
class MININGSPICECOPILOT_API IThreadSafetyInterface
{
public:
    /** Virtual destructor */
    virtual ~IThreadSafetyInterface() {}

    /**
     * Locks the resource
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     * @return True if the lock was acquired
     */
    virtual bool Lock(uint32 TimeoutMs = 0) = 0;
    
    /** Unlocks the resource */
    virtual void Unlock() = 0;
    
    /** Tries to lock without waiting */
    virtual bool TryLock() = 0;
    
    /** Gets whether the resource is currently locked */
    virtual bool IsLocked() const = 0;
}; 