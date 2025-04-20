#include "TaskHelpers.h"
#include "Interfaces/ITaskScheduler.h"
#include "../Public/TaskScheduler.h"

// Implementation of ITaskScheduler::Get() static method
ITaskScheduler& ITaskScheduler::Get()
{
    if (!FTaskScheduler::Instance)
    {
        FTaskScheduler::Instance = new FTaskScheduler();
        FTaskScheduler::Instance->Initialize();
    }
    return *FTaskScheduler::Instance;
} 