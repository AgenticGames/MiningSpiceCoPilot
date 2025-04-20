#include "../../Public/Interfaces/IAsyncOperation.h"
#include "../../Public/AsyncTaskManager.h"

// Implementation of static Get method
IAsyncOperation& IAsyncOperation::Get()
{
    // Return the AsyncTaskManager instance which implements IAsyncOperation
    return FAsyncTaskManager::Get();
} 