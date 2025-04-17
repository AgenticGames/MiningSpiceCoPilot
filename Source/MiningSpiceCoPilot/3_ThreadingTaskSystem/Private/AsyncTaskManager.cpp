// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncTaskManager.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Queue.h"
#include "Misc/App.h"
#include "PriorityTaskQueue.h"
#include "ThreadSafeOperationQueue.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CsvProfiler.h"

// Stats declarations
DECLARE_CYCLE_STAT(TEXT("Async Operation Execute"), STAT_AsyncOperation_Execute, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("Async Operation Progress"), STAT_AsyncOperation_Progress, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("Async Manager Update"), STAT_AsyncManager_Update, STATGROUP_Threading);

// CSV profiler category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Initialize static instance to nullptr
FAsyncTaskManager* FAsyncTaskManager::Instance = nullptr;

//----------------------------------------------------------------------
// FAsyncOperationImpl Implementation
//----------------------------------------------------------------------

FAsyncOperationImpl::FAsyncOperationImpl(uint64 InId, const FString& InType, const FString& InName)
    : Id(InId)
    , Type(InType)
    , Name(InName.IsEmpty() ? FString::Printf(TEXT("%s_%llu"), *Type, Id) : InName)
    , Status(EAsyncStatus::NotStarted)
    , ProgressUpdateIntervalSeconds(0.1) // Default to 100ms
    , LastProgressUpdateTime(0.0)
    , CreationTime(FPlatformTime::Seconds())
    , StartTime(0.0)
    , CompletionTime(0.0)
{
    Progress.CompletionPercentage = 0.0f;
    Progress.CurrentStage = 0;
    Progress.TotalStages = 1;
    Progress.ElapsedTimeSeconds = 0.0;
    Progress.EstimatedTimeRemainingSeconds = -1.0;
    Progress.ItemsProcessed = 0;
    Progress.TotalItems = 0;
    bCancelled.Set(0);
}

FAsyncOperationImpl::~FAsyncOperationImpl()
{
    // Ensure any callbacks are detached
    ProgressCallback.Unbind();
    CompletionCallback.Unbind();
}

uint64 FAsyncOperationImpl::GetId() const
{
    return Id;
}

const FString& FAsyncOperationImpl::GetType() const
{
    return Type;
}

const FString& FAsyncOperationImpl::GetName() const
{
    return Name;
}

void FAsyncOperationImpl::SetStatus(EAsyncStatus InStatus)
{
    FScopeLock Lock(&StateLock);
    Status = InStatus;
    
    // Update start time if transitioning to InProgress
    if (InStatus == EAsyncStatus::InProgress && StartTime <= 0.0)
    {
        StartTime = FPlatformTime::Seconds();
    }
    
    // Update completion time if reaching a terminal state
    if ((InStatus == EAsyncStatus::Completed || 
         InStatus == EAsyncStatus::Failed || 
         InStatus == EAsyncStatus::Cancelled ||
         InStatus == EAsyncStatus::TimedOut) && 
        CompletionTime <= 0.0)
    {
        CompletionTime = FPlatformTime::Seconds();
    }
}

EAsyncStatus FAsyncOperationImpl::GetStatus() const
{
    FScopeLock Lock(&StateLock);
    return Status;
}

void FAsyncOperationImpl::UpdateProgress(const FAsyncProgress& InProgress)
{
    FScopeLock Lock(&StateLock);
    
    // Update progress data
    Progress = InProgress;
    
    // Calculate elapsed time if not set
    if (Progress.ElapsedTimeSeconds <= 0.0 && StartTime > 0.0)
    {
        Progress.ElapsedTimeSeconds = FPlatformTime::Seconds() - StartTime;
    }
    
    // Update last progress time
    LastProgressUpdateTime = FPlatformTime::Seconds();
    
    // Notify about progress
    NotifyProgress();
}

FAsyncProgress FAsyncOperationImpl::GetProgress() const
{
    FScopeLock Lock(&StateLock);
    
    // If in progress, calculate current elapsed time
    FAsyncProgress CurrentProgress = Progress;
    if (Status == EAsyncStatus::InProgress && StartTime > 0.0)
    {
        CurrentProgress.ElapsedTimeSeconds = FPlatformTime::Seconds() - StartTime;
    }
    
    return CurrentProgress;
}

void FAsyncOperationImpl::SetResult(const FAsyncResult& InResult)
{
    FScopeLock Lock(&StateLock);
    Result = InResult;
}

FAsyncResult FAsyncOperationImpl::GetResult() const
{
    FScopeLock Lock(&StateLock);
    return Result;
}

bool FAsyncOperationImpl::RegisterProgressCallback(const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs)
{
    if (!Callback.IsBound())
    {
        return false;
    }
    
    FScopeLock Lock(&StateLock);
    
    ProgressCallback = Callback;
    ProgressUpdateIntervalSeconds = FMath::Max(0.01, UpdateIntervalMs / 1000.0);
    
    // Trigger initial progress notification
    if (Status == EAsyncStatus::InProgress)
    {
        NotifyProgress();
    }
    
    return true;
}

bool FAsyncOperationImpl::RegisterCompletionCallback(const FAsyncCompletionDelegate& Callback)
{
    if (!Callback.IsBound())
    {
        return false;
    }
    
    FScopeLock Lock(&StateLock);
    
    CompletionCallback = Callback;
    
    // If already completed, trigger immediately
    if (Status == EAsyncStatus::Completed || 
        Status == EAsyncStatus::Failed || 
        Status == EAsyncStatus::Cancelled ||
        Status == EAsyncStatus::TimedOut)
    {
        NotifyCompletion();
    }
    
    return true;
}

void FAsyncOperationImpl::SetParameters(const TMap<FString, FString>& Params)
{
    FScopeLock Lock(&StateLock);
    Parameters = Params;
}

const TMap<FString, FString>& FAsyncOperationImpl::GetParameters() const
{
    FScopeLock Lock(&StateLock);
    return Parameters;
}

void FAsyncOperationImpl::NotifyCompletion()
{
    // Make a local copy of the callback to avoid calling it while locked
    FAsyncCompletionDelegate CallbackCopy;
    FAsyncResult ResultCopy;
    
    {
        FScopeLock Lock(&StateLock);
        CallbackCopy = CompletionCallback;
        ResultCopy = Result;
    }
    
    // Call the completion callback if bound
    if (CallbackCopy.IsBound())
    {
        CallbackCopy.Execute(ResultCopy);
    }
}

void FAsyncOperationImpl::NotifyProgress()
{
    // Make a local copy of the callback to avoid calling it while locked
    FAsyncProgressDelegate CallbackCopy;
    FAsyncProgress ProgressCopy;
    
    {
        FScopeLock Lock(&StateLock);
        CallbackCopy = ProgressCallback;
        ProgressCopy = Progress;
        
        // If in progress, update elapsed time
        if (Status == EAsyncStatus::InProgress && StartTime > 0.0)
        {
            ProgressCopy.ElapsedTimeSeconds = FPlatformTime::Seconds() - StartTime;
        }
    }
    
    // Call the progress callback if bound
    if (CallbackCopy.IsBound())
    {
        CallbackCopy.Execute(ProgressCopy);
    }
}

double FAsyncOperationImpl::GetCreationTime() const
{
    return CreationTime;
}

double FAsyncOperationImpl::GetStartTime() const
{
    FScopeLock Lock(&StateLock);
    return StartTime;
}

double FAsyncOperationImpl::GetCompletionTime() const
{
    FScopeLock Lock(&StateLock);
    return CompletionTime;
}

void FAsyncOperationImpl::SetStartTime(double Time)
{
    FScopeLock Lock(&StateLock);
    StartTime = Time;
}

void FAsyncOperationImpl::SetCompletionTime(double Time)
{
    FScopeLock Lock(&StateLock);
    CompletionTime = Time;
}

bool FAsyncOperationImpl::IsProgressUpdateDue() const
{
    FScopeLock Lock(&StateLock);
    
    // Always allow updates if there's no previous update time
    if (LastProgressUpdateTime <= 0.0)
    {
        return true;
    }
    
    // Check if enough time has passed since last update
    double CurrentTime = FPlatformTime::Seconds();
    return (CurrentTime - LastProgressUpdateTime) >= ProgressUpdateIntervalSeconds;
}

//----------------------------------------------------------------------
// FAsyncOperationFactory Implementation
//----------------------------------------------------------------------

FAsyncOperationFactory::FAsyncOperationFactory()
{
}

FAsyncOperationFactory::~FAsyncOperationFactory()
{
    FScopeLock Lock(&CreatorsLock);
    OperationCreators.Empty();
}

bool FAsyncOperationFactory::RegisterOperationType(const FString& Type, TFunction<FAsyncOperationImpl*(uint64, const FString&)> Creator)
{
    if (Type.IsEmpty() || !Creator)
    {
        return false;
    }
    
    FScopeLock Lock(&CreatorsLock);
    
    if (OperationCreators.Contains(Type))
    {
        return false; // Already registered
    }
    
    OperationCreators.Add(Type, Creator);
    return true;
}

FAsyncOperationImpl* FAsyncOperationFactory::CreateOperation(uint64 Id, const FString& Type, const FString& Name)
{
    FScopeLock Lock(&CreatorsLock);
    
    auto Creator = OperationCreators.Find(Type);
    if (!Creator)
    {
        return nullptr;
    }
    
    return (*Creator)(Id, Name);
}

TArray<FString> FAsyncOperationFactory::GetRegisteredTypes() const
{
    FScopeLock Lock(&CreatorsLock);
    
    TArray<FString> Types;
    OperationCreators.GetKeys(Types);
    return Types;
}

bool FAsyncOperationFactory::IsTypeRegistered(const FString& Type) const
{
    FScopeLock Lock(&CreatorsLock);
    return OperationCreators.Contains(Type);
}

//----------------------------------------------------------------------
// FAsyncTaskManager Implementation
//----------------------------------------------------------------------

IAsyncOperation& FAsyncTaskManager::Get()
{
    // Lazy initialization if needed
    if (!Instance)
    {
        Instance = new FAsyncTaskManager();
        Instance->Initialize();
    }
    
    return *Instance;
}

FAsyncTaskManager::FAsyncTaskManager()
    : bIsInitialized(false)
    , NextOperationId(1)
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FAsyncTaskManager::~FAsyncTaskManager()
{
    Shutdown();
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

bool FAsyncTaskManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    bIsInitialized = true;
    NextOperationId.Set(1);
    
    // Register ticker delegate for progress updates
    UpdateTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FAsyncTaskManager::TickUpdateOperations),
        0.1f // 100ms interval
    );
    // Save the ticker handle as FTSTicker::FDelegateHandle type
    
    // Register built-in operation types
    // This would be expanded with mining-specific operations in a real implementation
    
    return true;
}

void FAsyncTaskManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Unregister ticker
    if (UpdateTimerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(UpdateTimerHandle);
    }
    
    // Clean up operations
    CleanupCompletedOperations(0.0);
    
    bIsInitialized = false;
}

bool FAsyncTaskManager::IsInitialized() const
{
    return bIsInitialized;
}

uint64 FAsyncTaskManager::CreateOperation(const FString& OperationType, const FString& OperationName)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    if (!Factory.IsTypeRegistered(OperationType))
    {
        UE_LOG(LogTemp, Warning, TEXT("AsyncTaskManager: Unknown operation type: %s"), *OperationType);
        return 0;
    }
    
    // Generate a new ID
    uint64 NewId = GenerateOperationId();
    
    // Create the operation
    FAsyncOperationImpl* NewOperation = Factory.CreateOperation(NewId, OperationType, OperationName);
    if (!NewOperation)
    {
        UE_LOG(LogTemp, Warning, TEXT("AsyncTaskManager: Failed to create operation of type: %s"), *OperationType);
        return 0;
    }
    
    // Add to the map
    {
        FScopeLock Lock(&OperationsLock);
        Operations.Add(NewId, NewOperation);
        UpdateActiveOperationsMap(OperationType, NewId, true);
    }
    
    return NewId;
}

bool FAsyncTaskManager::StartOperation(uint64 OperationId, const TMap<FString, FString>& Parameters)
{
    FScopeLock Lock(&OperationsLock);
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    // Skip if already started
    if (Operation->GetStatus() != EAsyncStatus::NotStarted)
    {
        return false;
    }
    
    // Apply parameters
    Operation->SetParameters(Parameters);
    
    // Update status
    Operation->SetStatus(EAsyncStatus::InProgress);
    
    // Record start time
    Operation->SetStartTime(FPlatformTime::Seconds());
    
    // Create task and start in background
    FAsyncOperationTaskWrapper* Task = new FAsyncOperationTaskWrapper(Operation);
    Task->StartBackgroundTask();
    
    return true;
}

bool FAsyncTaskManager::CancelOperation(uint64 OperationId, bool bWaitForCancellation)
{
    if (!bIsInitialized || OperationId == 0)
    {
        return false;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    EAsyncStatus CurrentStatus = Operation->GetStatus();
    if (CurrentStatus == EAsyncStatus::Completed || 
        CurrentStatus == EAsyncStatus::Failed || 
        CurrentStatus == EAsyncStatus::Cancelled || 
        CurrentStatus == EAsyncStatus::TimedOut)
    {
        return false;
    }
    
    // Set status and result
    Operation->bCancelled.Set(1);
    Operation->SetStatus(EAsyncStatus::Cancelled);
    Operation->SetResult(FAsyncResult::Cancelled());
    
    // Attempt to cancel the operation
    bool bSuccess = Operation->Cancel();
    
    if (bWaitForCancellation)
    {
        // Wait for the operation to be marked as complete
        uint32 TimeoutCounter = 0;
        while (Operation->GetStatus() == EAsyncStatus::InProgress && TimeoutCounter < 100) // 10 seconds max
        {
            FPlatformProcess::Sleep(0.1f);
            TimeoutCounter++;
        }
    }
    
    return bSuccess;
}

EAsyncStatus FAsyncTaskManager::GetOperationStatus(uint64 OperationId) const
{
    if (!bIsInitialized || OperationId == 0)
    {
        return EAsyncStatus::Invalid;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return EAsyncStatus::Invalid;
    }
    
    return Operation->GetStatus();
}

FAsyncProgress FAsyncTaskManager::GetOperationProgress(uint64 OperationId) const
{
    FAsyncProgress EmptyProgress;
    
    if (!bIsInitialized || OperationId == 0)
    {
        return EmptyProgress;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return EmptyProgress;
    }
    
    return Operation->GetProgress();
}

FAsyncResult FAsyncTaskManager::GetOperationResult(uint64 OperationId) const
{
    FAsyncResult EmptyResult;
    EmptyResult.bSuccess = false;
    EmptyResult.ErrorMessage = TEXT("Invalid operation ID");
    
    if (!bIsInitialized || OperationId == 0)
    {
        return EmptyResult;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return EmptyResult;
    }
    
    return Operation->GetResult();
}

bool FAsyncTaskManager::WaitForCompletion(uint64 OperationId, uint32 TimeoutMs)
{
    if (!bIsInitialized || OperationId == 0)
    {
        return false;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    EAsyncStatus Status = Operation->GetStatus();
    if (Status == EAsyncStatus::Completed)
    {
        return true;
    }
    
    if (Status == EAsyncStatus::Failed || 
        Status == EAsyncStatus::Cancelled || 
        Status == EAsyncStatus::TimedOut)
    {
        return false;
    }
    
    // Calculate timeout time
    double EndTime = FPlatformTime::Seconds();
    if (TimeoutMs > 0)
    {
        EndTime += TimeoutMs / 1000.0;
    }
    else
    {
        EndTime = DBL_MAX; // Effectively infinite timeout
    }
    
    // Wait for completion or timeout
    while (FPlatformTime::Seconds() < EndTime)
    {
        Status = Operation->GetStatus();
        
        if (Status == EAsyncStatus::Completed)
        {
            return true;
        }
        
        if (Status == EAsyncStatus::Failed || 
            Status == EAsyncStatus::Cancelled || 
            Status == EAsyncStatus::TimedOut)
        {
            return false;
        }
        
        // Sleep to avoid spinning too hard
        FPlatformProcess::Sleep(0.01f);
    }
    
    // If we got here, we timed out
    if (TimeoutMs > 0)
    {
        Operation->SetStatus(EAsyncStatus::TimedOut);
        Operation->SetResult(FAsyncResult::TimedOut());
        Operation->NotifyCompletion();
    }
    
    return false;
}

bool FAsyncTaskManager::RegisterProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs)
{
    if (!bIsInitialized || OperationId == 0 || !Callback.IsBound())
    {
        return false;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    return Operation->RegisterProgressCallback(Callback, UpdateIntervalMs);
}

bool FAsyncTaskManager::RegisterCompletionCallback(uint64 OperationId, const FAsyncCompletionDelegate& Callback)
{
    if (!bIsInitialized || OperationId == 0 || !Callback.IsBound())
    {
        return false;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    return Operation->RegisterCompletionCallback(Callback);
}

uint32 FAsyncTaskManager::GetActiveOperationCount() const
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&OperationsLock);
    
    uint32 Count = 0;
    for (const auto& Pair : Operations)
    {
        FAsyncOperationImpl* Operation = Pair.Value;
        if (!Operation)
        {
            continue;
        }
        
        EAsyncStatus Status = Operation->GetStatus();
        if (Status == EAsyncStatus::NotStarted || Status == EAsyncStatus::InProgress)
        {
            Count++;
        }
    }
    
    return Count;
}

TArray<uint64> FAsyncTaskManager::GetActiveOperations() const
{
    TArray<uint64> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&OperationsLock);
    
    for (const auto& Pair : Operations)
    {
        FAsyncOperationImpl* Operation = Pair.Value;
        if (!Operation)
        {
            continue;
        }
        
        EAsyncStatus Status = Operation->GetStatus();
        if (Status == EAsyncStatus::NotStarted || Status == EAsyncStatus::InProgress)
        {
            Result.Add(Pair.Key);
        }
    }
    
    return Result;
}

TArray<uint64> FAsyncTaskManager::GetOperationsOfType(const FString& OperationType) const
{
    TArray<uint64> Result;
    
    if (!bIsInitialized || OperationType.IsEmpty())
    {
        return Result;
    }
    
    FScopeLock Lock(&OperationsLock);
    
    const TArray<uint64>* TypeOperations = ActiveOperationsByType.Find(OperationType);
    if (TypeOperations)
    {
        Result = *TypeOperations;
    }
    
    return Result;
}

uint32 FAsyncTaskManager::CleanupCompletedOperations(double MaxAgeSeconds)
{
    if (!bIsInitialized || MaxAgeSeconds <= 0.0)
    {
        return 0;
    }
    
    uint32 CleanedCount = 0;
    double CurrentTime = FPlatformTime::Seconds();
    double MinCompletionTime = CurrentTime - MaxAgeSeconds;
    TArray<uint64> IdsToRemove;
    
    // Identify operations to remove
    {
        FScopeLock Lock(&OperationsLock);
        
        for (const auto& Pair : Operations)
        {
            FAsyncOperationImpl* Operation = Pair.Value;
            if (!Operation)
            {
                IdsToRemove.Add(Pair.Key);
                continue;
            }
            
            EAsyncStatus Status = Operation->GetStatus();
            if (Status != EAsyncStatus::NotStarted && Status != EAsyncStatus::InProgress)
            {
                double CompletionTime = Operation->GetCompletionTime();
                if (CompletionTime > 0.0 && CompletionTime < MinCompletionTime)
                {
                    IdsToRemove.Add(Pair.Key);
                }
            }
        }
    }
    
    // Remove identified operations
    for (uint64 Id : IdsToRemove)
    {
        // Get the operation first
        FAsyncOperationImpl* Operation = nullptr;
        {
            FScopeLock Lock(&OperationsLock);
            Operation = Operations.FindRef(Id);
            if (Operation)
            {
                // Remove from maps
                Operations.Remove(Id);
                UpdateActiveOperationsMap(Operation->GetType(), Id, false);
                CleanedCount++;
            }
        }
        
        // Delete the operation outside the lock
        delete Operation;
    }
    
    return CleanedCount;
}

bool FAsyncTaskManager::RegisterOperationType(const FString& Type, TFunction<FAsyncOperationImpl*(uint64, const FString&)> Creator)
{
    if (!bIsInitialized || Type.IsEmpty() || !Creator)
    {
        return false;
    }
    
    return Factory.RegisterOperationType(Type, Creator);
}

TArray<FString> FAsyncTaskManager::GetRegisteredOperationTypes() const
{
    if (!bIsInitialized)
    {
        return TArray<FString>();
    }
    
    return Factory.GetRegisteredTypes();
}

void FAsyncTaskManager::UpdateOperations()
{
    SCOPE_CYCLE_COUNTER(STAT_AsyncManager_Update);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    // Make a copy of active operations to avoid holding the lock while iterating
    TArray<uint64> ActiveIds;
    {
        FScopeLock Lock(&OperationsLock);
        
        for (const auto& Pair : Operations)
        {
            FAsyncOperationImpl* Operation = Pair.Value;
            if (!Operation)
            {
                continue;
            }
            
            EAsyncStatus Status = Operation->GetStatus();
            if (Status == EAsyncStatus::InProgress)
            {
                ActiveIds.Add(Pair.Key);
            }
        }
    }
    
    // Update progress for each active operation
    for (uint64 Id : ActiveIds)
    {
        FAsyncOperationImpl* Operation = GetOperationById(Id);
        if (Operation && Operation->GetStatus() == EAsyncStatus::InProgress)
        {
            // Check if it's time for a progress update
            if (Operation->IsProgressUpdateDue())
            {
                SCOPE_CYCLE_COUNTER(STAT_AsyncOperation_Progress);
                Operation->NotifyProgress();
            }
        }
    }
}

FAsyncOperationImpl* FAsyncTaskManager::GetOperationById(uint64 OperationId) const
{
    if (OperationId == 0)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&OperationsLock);
    return Operations.FindRef(OperationId);
}

void FAsyncTaskManager::OnOperationCompleted(uint64 OperationId, const FAsyncResult& Result)
{
    if (OperationId == 0)
    {
        return;
    }
    
    // Get the operation
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return;
    }
    
    // Move to completed state
    MoveToCompleted(Operation);
}

FString FAsyncTaskManager::CreateProgressDelta(const FAsyncProgress& Previous, const FAsyncProgress& Current)
{
    TArray<FString> Changes;
    
    if (Current.CompletionPercentage != Previous.CompletionPercentage)
    {
        Changes.Add(FString::Printf(TEXT("Progress: %.1f%% -> %.1f%%"), 
            Previous.CompletionPercentage * 100.0f, Current.CompletionPercentage * 100.0f));
    }
    
    if (Current.CurrentStage != Previous.CurrentStage)
    {
        Changes.Add(FString::Printf(TEXT("Stage: %d -> %d"), 
            Previous.CurrentStage, Current.CurrentStage));
    }
    
    if (Current.ItemsProcessed != Previous.ItemsProcessed)
    {
        Changes.Add(FString::Printf(TEXT("Items: %lld -> %lld"), 
            Previous.ItemsProcessed, Current.ItemsProcessed));
    }
    
    if (Current.StatusMessage != Previous.StatusMessage)
    {
        Changes.Add(FString::Printf(TEXT("Status: %s -> %s"), 
            *Previous.StatusMessage, *Current.StatusMessage));
    }
    
    return FString::Join(Changes, TEXT(", "));
}

uint64 FAsyncTaskManager::GenerateOperationId()
{
    return NextOperationId.Add(1);
}

void FAsyncTaskManager::MoveToCompleted(FAsyncOperationImpl* Operation)
{
    if (!Operation)
    {
        return;
    }
    
    FScopeLock Lock(&OperationsLock);
    
    uint64 OperationId = Operation->GetId();
    const FString& OperationType = Operation->GetType();
    
    // Update operation status and completion time if not already set
    if (Operation->GetCompletionTime() <= 0.0)
    {
        Operation->SetCompletionTime(FPlatformTime::Seconds());
    }
    
    // Remove from active operations by type tracking
    UpdateActiveOperationsMap(OperationType, OperationId, false);
}

void FAsyncTaskManager::UpdateActiveOperationsMap(const FString& Type, uint64 Id, bool bAdd)
{
    if (Type.IsEmpty() || Id == 0)
    {
        return;
    }
    
    // Note: OperationsLock should already be acquired by the caller
    
    if (bAdd)
    {
        // Add to type map
        TArray<uint64>& TypeOperations = ActiveOperationsByType.FindOrAdd(Type);
        TypeOperations.AddUnique(Id);
    }
    else
    {
        // Remove from type map
        TArray<uint64>* TypeOperations = ActiveOperationsByType.Find(Type);
        if (TypeOperations)
        {
            TypeOperations->Remove(Id);
            
            // Remove empty arrays
            if (TypeOperations->Num() == 0)
            {
                ActiveOperationsByType.Remove(Type);
            }
        }
    }
}

// Tick callback for updating operations
bool FAsyncTaskManager::TickUpdateOperations(float DeltaTime)
{
    UpdateOperations();
    return true;
}

//----------------------------------------------------------------------
// FAsyncOperationTask Implementation
//----------------------------------------------------------------------

void FAsyncOperationTask::DoWork()
{
    if (!Operation)
    {
        return;
    }
    
    // Execute the operation
    bool bSuccess = Operation->Execute();
    
    // Set the operation status based on the result
    if (Operation->bCancelled.GetValue() != 0)
    {
        Operation->SetStatus(EAsyncStatus::Cancelled);
    }
    else if (bSuccess)
    {
        Operation->SetStatus(EAsyncStatus::Completed);
    }
    else
    {
        Operation->SetStatus(EAsyncStatus::Failed);
    }
    
    // Notify the operation completion
    Operation->NotifyCompletion();
}

void FAsyncOperationTask::Abandon()
{
    if (Operation)
    {
        Operation->bCancelled.Set(1);
        Operation->SetStatus(EAsyncStatus::Cancelled);
        Operation->NotifyCompletion();
    }
}