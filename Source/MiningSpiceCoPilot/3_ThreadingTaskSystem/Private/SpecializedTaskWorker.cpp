#include "../Public/TaskScheduler.h"

// Implementation of FSpecializedTaskWorker::SelectNextTask
FMiningTask* FSpecializedTaskWorker::SelectNextTask()
{
    // Find a task that matches this worker's specialized capabilities
    FTaskScheduler* SchedulerPtr = GetScheduler();
    if (!SchedulerPtr)
    {
        return nullptr;
    }
    
    // Find tasks that match this worker's capabilities
    // We can use the base class implementation as a fallback
    return FMiningTaskWorker::SelectNextTask();
} 