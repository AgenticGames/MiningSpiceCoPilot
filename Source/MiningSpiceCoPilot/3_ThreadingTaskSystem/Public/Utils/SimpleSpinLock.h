#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformAtomics.h"

/**
 * High-performance spin lock implementation optimized for low contention scenarios
 * Uses exponential backoff to reduce CPU usage under contention
 */
class MININGSPICECOPILOT_API FSimpleSpinLock
{
private:
    /** The lock state - 0 means unlocked, 1 means locked */
    volatile int32 LockState;

public:
    /** Constructor */
    FORCEINLINE FSimpleSpinLock() : LockState(0) {}

    /** Destructor */
    FORCEINLINE ~FSimpleSpinLock() {}

    /**
     * Acquires the lock, spinning until successful
     * Uses exponential backoff to reduce CPU usage during contention
     */
    FORCEINLINE void Lock()
    {
        // First attempt - optimistic lock acquisition without backoff
        if (TryLock())
        {
            return;
        }

        // Enter contention path with exponential backoff
        uint32 SpinCount = 0;
        while (!TryLock())
        {
            // Exponential backoff - prevents cache line thrashing and excessive CPU usage
            if (SpinCount < 10u) // Fixed: using unsigned literal
            {
                // Initial backoff - use CPU yield/pause instruction
                for (uint32 i = 0; i < (1u << SpinCount); ++i)
                {
                    FPlatformProcess::Yield();
                }
            }
            else if (SpinCount < 20u) // Fixed: using unsigned literal
            {
                // Medium backoff - sleep for short periods
                FPlatformProcess::SleepNoStats(0.0001f); // 0.1ms
            }
            else
            {
                // Long backoff - sleep for longer periods
                FPlatformProcess::SleepNoStats(0.001f); // 1ms
            }
            
            // Increment spin count, capping at 30 to prevent overflow
            SpinCount = FMath::Min(SpinCount + 1, 30u);
        }
    }

    /**
     * Attempts to acquire the lock without spinning
     * @return True if lock was acquired, false otherwise
     */
    FORCEINLINE bool TryLock()
    {
        // Atomically exchange 0 with 1, returns the previous value
        return FPlatformAtomics::InterlockedCompareExchange(&LockState, 1, 0) == 0;
    }

    /**
     * Releases the lock
     */
    FORCEINLINE void Unlock()
    {
        // Memory barrier to ensure all writes while lock was held are visible
        FPlatformAtomics::InterlockedExchange(&LockState, 0); // Fixed: Using InterlockedExchange instead of MemoryBarrier
    }

    /**
     * Checks if the lock is currently held
     * @return True if locked, false if unlocked
     */
    FORCEINLINE bool IsLocked() const
    {
        return LockState != 0;
    }
};
