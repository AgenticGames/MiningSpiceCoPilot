#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITaskScheduler.h"
#include "ParallelExecutor.h"

/**
 * Helper function to schedule tasks with the global task scheduler
 * 
 * @param TaskFunc The task function to execute
 * @param Config The task configuration
 * @return The scheduled task ID
 */
inline uint64 ScheduleTaskWithScheduler(TFunction<void()> TaskFunc, const FTaskConfig& Config)
{
    return ITaskScheduler::Get().ScheduleTask(TaskFunc, Config);
}

/**
 * Helper functions for parallel processing in the mining system
 */
class MININGSPICECOPILOT_API FTaskHelpers
{
public:
    /**
     * Executes a function for each item in parallel, blocking until completion
     * @param ItemCount Number of items to process
     * @param Function Function to execute for each item
     * @param Config Optional parallel configuration
     * @return True if execution was successful
     */
    static bool ParallelForBlocking(int32 ItemCount, TFunction<void(int32)> Function, 
        const FParallelConfig& Config = FParallelConfig())
    {
        if (ItemCount <= 0 || !Function)
        {
            return false;
        }
        
        bool bResult = FParallelExecutor::Get().ParallelFor(ItemCount, Function, Config);
        
        // Wait for completion
        FParallelExecutor::Get().Wait();
        
        return bResult;
    }
    
    /**
     * Executes a function for each range of items in parallel, blocking until completion
     * @param ItemCount Number of items to process
     * @param Function Function to execute for each range (start, end)
     * @param Config Optional parallel configuration
     * @return True if execution was successful
     */
    static bool ParallelForRangeBlocking(int32 ItemCount, TFunction<void(int32, int32)> Function, 
        const FParallelConfig& Config = FParallelConfig())
    {
        if (ItemCount <= 0 || !Function)
        {
            return false;
        }
        
        bool bResult = FParallelExecutor::Get().ParallelForRange(ItemCount, Function, Config);
        
        // Wait for completion
        FParallelExecutor::Get().Wait();
        
        return bResult;
    }
}; 