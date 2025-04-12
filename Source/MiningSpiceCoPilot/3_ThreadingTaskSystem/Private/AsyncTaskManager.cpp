// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncTaskManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"
#include "HAL/Event.h"

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
}

EAsyncStatus FAsyncOperationImpl::GetStatus() const
{
    FScopeLock Lock(&StateLock);
    return Status;
}

void FAsyncOperationImpl::UpdateProgress(const FAsyncProgress& InProgress)
{
    FScopeLock Lock(&StateLock);
    Progress = InProgress;
    
    // Update elapsed time
    if (StartTime > 0.0)
    {
        Progress.ElapsedTimeSeconds = FPlatformTime::Seconds() - StartTime;
    }
    
    // Calculate estimated time remaining if we have a valid completion percentage
    if (Progress.CompletionPercentage > 0.0f && Progress.CompletionPercentage < 1.0f)
    {
        Progress.EstimatedTimeRemainingSeconds = (Progress.ElapsedTimeSeconds / Progress.CompletionPercentage) * (1.0f - Progress.CompletionPercentage);
    }
    
    LastProgressUpdateTime = FPlatformTime::Seconds();
    
    // Trigger progress callback
    if (Status == EAsyncStatus::InProgress)
    {
        NotifyProgress();
    }
}

FAsyncProgress FAsyncOperationImpl::GetProgress() const
{
    FScopeLock Lock(&StateLock);
    
    // Update elapsed time in the copy we return
    FAsyncProgress CurrentProgress = Progress;
    if (StartTime > 0.0)
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
    if (Status == EAsyncStatus::Completed || Status == EAsyncStatus::Failed || 
        Status == EAsyncStatus::Cancelled || Status == EAsyncStatus::TimedOut)
    {
        return false;
    }
    
    FScopeLock Lock(&StateLock);
    ProgressCallback = Callback;
    ProgressUpdateIntervalSeconds = UpdateIntervalMs / 1000.0;
    return true;
}

bool FAsyncOperationImpl::RegisterCompletionCallback(const FAsyncCompletionDelegate& Callback)
{
    if (Status == EAsyncStatus::Completed || Status == EAsyncStatus::Failed || 
        Status == EAsyncStatus::Cancelled || Status == EAsyncStatus::TimedOut)
    {
        // Operation already completed, execute the callback immediately
        if (Callback.IsBound())
        {
            Callback.Execute(Result);
        }
        return false;
    }
    
    FScopeLock Lock(&StateLock);
    CompletionCallback = Callback;
    return true;
}

void FAsyncOperationImpl::SetParameters(const TMap<FString, FString>& Params)
{
    FScopeLock Lock(&StateLock);
    Parameters = Params;
}

const TMap<FString, FString>& FAsyncOperationImpl::GetParameters() const
{
    return Parameters;
}

void FAsyncOperationImpl::NotifyCompletion()
{
    FScopeLock Lock(&StateLock);
    
    // Set completion time
    CompletionTime = FPlatformTime::Seconds();
    
    // Update final progress
    if (Result.bSuccess && Status == EAsyncStatus::InProgress)
    {
        Progress.CompletionPercentage = 1.0f;
        Progress.EstimatedTimeRemainingSeconds = 0.0;
        Progress.ElapsedTimeSeconds = CompletionTime - StartTime;
    }
    
    // Execute completion callback
    if (CompletionCallback.IsBound())
    {
        CompletionCallback.Execute(Result);
    }
}

void FAsyncOperationImpl::NotifyProgress()
{
    // Only notify if the callback is bound and we're due for an update
    if (ProgressCallback.IsBound() && IsProgressUpdateDue())
    {
        ProgressCallback.Execute(Progress);
        LastProgressUpdateTime = FPlatformTime::Seconds();
    }
}

double FAsyncOperationImpl::GetCreationTime() const
{
    return CreationTime;
}

double FAsyncOperationImpl::GetStartTime() const
{
    return StartTime;
}

double FAsyncOperationImpl::GetCompletionTime() const
{
    return CompletionTime;
}

void FAsyncOperationImpl::SetStartTime(double Time)
{
    StartTime = Time;
}

void FAsyncOperationImpl::SetCompletionTime(double Time)
{
    CompletionTime = Time;
}

bool FAsyncOperationImpl::IsProgressUpdateDue() const
{
    return (FPlatformTime::Seconds() - LastProgressUpdateTime) >= ProgressUpdateIntervalSeconds;
}

//----------------------------------------------------------------------
// FAsyncOperationTask Implementation
//----------------------------------------------------------------------

FAsyncOperationTask::FAsyncOperationTask(FAsyncOperationImpl* InOperation)
    : Operation(InOperation)
{
    check(Operation != nullptr);
}

void FAsyncOperationTask::DoWork()
{
    if (!Operation)
    {
        return;
    }
    
    // Set status to in progress
    Operation->SetStatus(EAsyncStatus::InProgress);
    
    // Set start time
    Operation->SetStartTime(FPlatformTime::Seconds());
    
    // Execute the operation
    bool bSuccess = Operation->Execute();
    
    // Set status and result based on execution success
    if (bSuccess)
    {
        Operation->SetStatus(EAsyncStatus::Completed);
    }
    else
    {
        // Check if the operation was cancelled during execution
        if (Operation->GetStatus() == EAsyncStatus::Cancelled)
        {
            // Already set to cancelled, result should be set by the Cancel() method
        }
        else
        {
            // Otherwise, consider it a failure
            Operation->SetStatus(EAsyncStatus::Failed);
            
            // Set a default error result if one hasn't been set by the operation itself
            FAsyncResult CurrentResult = Operation->GetResult();
            if (CurrentResult.bSuccess)
            {
                Operation->SetResult(FAsyncResult(TEXT("Operation failed with no specific error information")));
            }
        }
    }
    
    // Set completion time and notify
    Operation->SetCompletionTime(FPlatformTime::Seconds());
    Operation->NotifyCompletion();
    
    // Get the AsyncTaskManager and notify it of completion
    FAsyncTaskManager* Manager = static_cast<FAsyncTaskManager*>(&IAsyncOperation::Get());
    if (Manager)
    {
        Manager->OnOperationCompleted(Operation->GetId(), Operation->GetResult());
    }
}

void FAsyncOperationTask::Abandon()
{
    if (Operation)
    {
        // Mark as cancelled
        Operation->SetStatus(EAsyncStatus::Cancelled);
        Operation->SetResult(FAsyncResult::Cancelled());
        Operation->NotifyCompletion();
        
        // Get the AsyncTaskManager and notify it of completion
        FAsyncTaskManager* Manager = static_cast<FAsyncTaskManager*>(&IAsyncOperation::Get());
        if (Manager)
        {
            Manager->OnOperationCompleted(Operation->GetId(), Operation->GetResult());
        }
    }
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

IAsyncOperation& IAsyncOperation::Get()
{
    check(FAsyncTaskManager::Instance != nullptr);
    return *FAsyncTaskManager::Instance;
}

FAsyncTaskManager::FAsyncTaskManager()
    : bIsInitialized(false)
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
    
    // Set up periodic update timer
    UpdateTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([this](float DeltaTime) {
            UpdateOperations();
            return true; // keep the ticker active
        }),
        1.0f // Update once per second
    );
    
    bIsInitialized = true;
    return true;
}

void FAsyncTaskManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Remove the ticker
    if (UpdateTimerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(UpdateTimerHandle);
        UpdateTimerHandle.Reset();
    }
    
    // Cancel all active operations
    TArray<uint64> OperationIds;
    {
        FScopeLock Lock(&OperationsLock);
        Operations.GetKeys(OperationIds);
    }
    
    for (uint64 Id : OperationIds)
    {
        CancelOperation(Id, false);
    }
    
    // Clean up all operations
    {
        FScopeLock Lock(&OperationsLock);
        
        for (auto& Pair : Operations)
        {
            delete Pair.Value;
        }
        
        Operations.Empty();
        ActiveOperationsByType.Empty();
    }
    
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
    if (!bIsInitialized || OperationId == 0)
    {
        return false;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (!Operation)
    {
        return false;
    }
    
    if (Operation->GetStatus() != EAsyncStatus::NotStarted)
    {
        return false;
    }
    
    // Set parameters
    if (Parameters.Num() > 0)
    {
        Operation->SetParameters(Parameters);
    }
    
    // Create and start the task
    FAsyncOperationTask* Task = new FAsyncOperationTask(Operation);
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
    Operation->SetStatus(EAsyncStatus::Cancelled);
    Operation->SetResult(FAsyncResult::Cancelled());
    
    // Attempt to cancel the operation
    bool bSuccess = Operation->Cancel();
    
    if (bWaitForCancellation)
    {
        // Wait for the operation to be marked as complete
        while (Operation->GetStatus() == EAsyncStatus::InProgress)
        {
            FPlatformProcess::Sleep(0.01f);
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
    
    EAsyncStatus CurrentStatus = Operation->GetStatus();
    if (CurrentStatus == EAsyncStatus::Completed)
    {
        return true;
    }
    
    if (CurrentStatus == EAsyncStatus::Failed || 
        CurrentStatus == EAsyncStatus::Cancelled || 
        CurrentStatus == EAsyncStatus::TimedOut)
    {
        return false;
    }
    
    // Wait for completion with timeout
    double StartWaitTime = FPlatformTime::Seconds();
    double TimeoutSeconds = TimeoutMs / 1000.0;
    
    while (true)
    {
        CurrentStatus = Operation->GetStatus();
        
        if (CurrentStatus == EAsyncStatus::Completed)
        {
            return true;
        }
        
        if (CurrentStatus == EAsyncStatus::Failed || 
            CurrentStatus == EAsyncStatus::Cancelled)
        {
            return false;
        }
        
        // Check for timeout
        if (TimeoutMs > 0)
        {
            double ElapsedSeconds = FPlatformTime::Seconds() - StartWaitTime;
            if (ElapsedSeconds >= TimeoutSeconds)
            {
                // Set operation status to timed out
                Operation->SetStatus(EAsyncStatus::TimedOut);
                Operation->SetResult(FAsyncResult::TimedOut());
                return false;
            }
        }
        
        // Sleep a bit to avoid spinning
        FPlatformProcess::Sleep(0.01f);
    }
    
    return false;
}

bool FAsyncTaskManager::RegisterProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs)
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
    
    return Operation->RegisterProgressCallback(Callback, UpdateIntervalMs);
}

bool FAsyncTaskManager::RegisterCompletionCallback(uint64 OperationId, const FAsyncCompletionDelegate& Callback)
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
        EAsyncStatus Status = Pair.Value->GetStatus();
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
        EAsyncStatus Status = Pair.Value->GetStatus();
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
    
    if (!bIsInitialized)
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
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&OperationsLock);
    
    uint32 CleanedCount = 0;
    double CurrentTime = FPlatformTime::Seconds();
    TArray<uint64> OperationsToRemove;
    
    for (const auto& Pair : Operations)
    {
        FAsyncOperationImpl* Operation = Pair.Value;
        EAsyncStatus Status = Operation->GetStatus();
        
        if (Status == EAsyncStatus::Completed || 
            Status == EAsyncStatus::Failed || 
            Status == EAsyncStatus::Cancelled || 
            Status == EAsyncStatus::TimedOut)
        {
            double CompletionTime = Operation->GetCompletionTime();
            if (CompletionTime > 0 && (CurrentTime - CompletionTime) >= MaxAgeSeconds)
            {
                OperationsToRemove.Add(Pair.Key);
            }
        }
    }
    
    // Remove operations from maps and delete
    for (uint64 Id : OperationsToRemove)
    {
        FAsyncOperationImpl* Operation = Operations.FindAndRemoveChecked(Id);
        UpdateActiveOperationsMap(Operation->GetType(), Id, false);
        delete Operation;
        CleanedCount++;
    }
    
    return CleanedCount;
}

bool FAsyncTaskManager::RegisterOperationType(const FString& Type, TFunction<FAsyncOperationImpl*(uint64, const FString&)> Creator)
{
    if (!bIsInitialized)
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
    if (!bIsInitialized)
    {
        return;
    }
    
    TArray<FAsyncOperationImpl*> OperationsToUpdate;
    
    {
        FScopeLock Lock(&OperationsLock);
        
        for (const auto& Pair : Operations)
        {
            FAsyncOperationImpl* Operation = Pair.Value;
            if (Operation->GetStatus() == EAsyncStatus::InProgress && Operation->IsProgressUpdateDue())
            {
                OperationsToUpdate.Add(Operation);
            }
        }
    }
    
    // Trigger progress notifications outside of lock
    for (FAsyncOperationImpl* Operation : OperationsToUpdate)
    {
        Operation->NotifyProgress();
    }
}

FAsyncOperationImpl* FAsyncTaskManager::GetOperationById(uint64 OperationId) const
{
    FScopeLock Lock(&OperationsLock);
    
    FAsyncOperationImpl** FoundOperation = Operations.Find(OperationId);
    return FoundOperation ? *FoundOperation : nullptr;
}

void FAsyncTaskManager::OnOperationCompleted(uint64 OperationId, const FAsyncResult& Result)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FAsyncOperationImpl* Operation = GetOperationById(OperationId);
    if (Operation)
    {
        MoveToCompleted(Operation);
    }
}

FString FAsyncTaskManager::CreateProgressDelta(const FAsyncProgress& Previous, const FAsyncProgress& Current)
{
    // Calculate the differences between two progress states
    FString Delta;
    
    if (Current.CompletionPercentage != Previous.CompletionPercentage)
    {
        Delta += FString::Printf(TEXT("Progress: %.1f%% -> %.1f%% | "), 
            Previous.CompletionPercentage * 100.0f, Current.CompletionPercentage * 100.0f);
    }
    
    if (Current.CurrentStage != Previous.CurrentStage)
    {
        Delta += FString::Printf(TEXT("Stage: %d/%d -> %d/%d | "), 
            Previous.CurrentStage, Previous.TotalStages, Current.CurrentStage, Current.TotalStages);
    }
    
    if (Current.ItemsProcessed != Previous.ItemsProcessed)
    {
        Delta += FString::Printf(TEXT("Items: %lld/%lld -> %lld/%lld | "), 
            Previous.ItemsProcessed, Previous.TotalItems, Current.ItemsProcessed, Current.TotalItems);
    }
    
    if (Current.EstimatedTimeRemainingSeconds != Previous.EstimatedTimeRemainingSeconds)
    {
        if (Current.EstimatedTimeRemainingSeconds > 0.0)
        {
            Delta += FString::Printf(TEXT("ETA: %.1fs"), Current.EstimatedTimeRemainingSeconds);
        }
        else
        {
            Delta += TEXT("ETA: Unknown");
        }
    }
    
    if (Current.StatusMessage != Previous.StatusMessage && !Current.StatusMessage.IsEmpty())
    {
        if (!Delta.IsEmpty())
        {
            Delta += TEXT(" | ");
        }
        Delta += FString::Printf(TEXT("Status: %s"), *Current.StatusMessage);
    }
    
    return Delta;
}

uint64 FAsyncTaskManager::GenerateOperationId()
{
    uint64 NewId = static_cast<uint64>(NextOperationId.Increment());
    
    // Make sure it's never 0, which is invalid
    if (NewId == 0)
    {
        NewId = static_cast<uint64>(NextOperationId.Increment());
    }
    
    return NewId;
}

void FAsyncTaskManager::MoveToCompleted(FAsyncOperationImpl* Operation)
{
    if (!Operation)
    {
        return;
    }
    
    // Update active operations map
    UpdateActiveOperationsMap(Operation->GetType(), Operation->GetId(), false);
}

void FAsyncTaskManager::UpdateActiveOperationsMap(const FString& Type, uint64 Id, bool bAdd)
{
    FScopeLock Lock(&OperationsLock);
    
    if (bAdd)
    {
        // Add to the type map
        TArray<uint64>& TypeOperations = ActiveOperationsByType.FindOrAdd(Type);
        TypeOperations.AddUnique(Id);
    }
    else
    {
        // Remove from the type map
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