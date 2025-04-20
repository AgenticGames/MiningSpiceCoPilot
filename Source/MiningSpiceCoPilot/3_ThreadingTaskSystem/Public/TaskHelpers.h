#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITaskScheduler.h"

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