#pragma once

#include "CoreMinimal.h"
#include "Async/TaskGraphInterfaces.h"

/**
 * Helper class for executing tasks without relying on the render thread
 */
class FSimplifiedTaskExecution
{
public:
    /**
     * Executes a task on a background thread
     */
    template<typename FunctorType>
    static void ExecuteOnBackgroundThread(FunctorType&& Task)
    {
        // Use task graph to execute the task on a background thread
        FFunctionGraphTask::CreateAndDispatchWhenReady(
            Forward<FunctorType>(Task),
            TStatId(),
            nullptr,
            ENamedThreads::AnyBackgroundThreadNormalTask
        );
    }
    
    /**
     * Executes a task on the game thread
     */
    template<typename FunctorType>
    static void ExecuteOnGameThread(FunctorType&& Task)
    {
        // Use task graph to execute the task on the game thread
        FFunctionGraphTask::CreateAndDispatchWhenReady(
            Forward<FunctorType>(Task),
            TStatId(),
            nullptr,
            ENamedThreads::GameThread
        );
    }
};