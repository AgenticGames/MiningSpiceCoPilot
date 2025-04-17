#include "MemoryDefragmenter.h"
#include "CoreMinimal.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"
#include "Interfaces/IMemoryTracker.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

FMemoryDefragmenter::FMemoryDefragmenter(IMemoryManager* InMemoryManager)
    : MemoryManager(InMemoryManager)
    , Thread(nullptr)
    , ThreadEvent(nullptr)
    , bShouldStop(false)
    , bIsPaused(false)
    , bInProgress(false)
    , Status(EDefragStatus::Idle)
    , FragmentationThreshold(20.0f)
    , bAutoDefragEnabled(true)
    , bThreadedDefragmentation(false)
{
}

FMemoryDefragmenter::~FMemoryDefragmenter()
{
    Stop();
}

bool FMemoryDefragmenter::Initialize()
{
    FScopeLock Lock(&DefragLock);
    
    if (bInProgress)
    {
        return false;
    }
    
    Stats.Reset();
    bShouldStop = false;
    bIsPaused = false;
    
    if (bThreadedDefragmentation)
    {
        // Create thread event
        if (!ThreadEvent)
        {
            ThreadEvent = FPlatformProcess::GetSynchEventFromPool(false);
        }
        
        // Create defragmentation thread
        if (!Thread)
        {
            Thread = FRunnableThread::Create(this, TEXT("MemoryDefragmenter"), 0, TPri_BelowNormal);
        }
    }
    
    UpdateStatus(EDefragStatus::Idle);
    return true;
}

void FMemoryDefragmenter::Shutdown()
{
    FScopeLock Lock(&DefragLock);
    
    if (Thread)
    {
        bShouldStop = true;
        ThreadEvent->Trigger();
        
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
    
    if (ThreadEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(ThreadEvent);
        ThreadEvent = nullptr;
    }
    
    // Clear all defragmentation queues
    DefragQueue.Empty();
    UpdateStatus(EDefragStatus::Idle);
}

bool FMemoryDefragmenter::ScheduleDefragmentation(const FName& PoolName, EDefragPriority Priority, float MaxTimeMs)
{
    FScopeLock Lock(&DefragLock);
    
    // Check if the pool exists
    if (!MemoryManager || PoolName.IsNone())
    {
        return false;
    }
    
    // Add to queue with priority and time
    TPair<EDefragPriority, float> PriorityTimePair(Priority, MaxTimeMs);
    TPair<FName, TPair<EDefragPriority, float>> QueueItem(PoolName, PriorityTimePair);
    DefragQueue.Enqueue(QueueItem);
    
    if (Status == EDefragStatus::Idle)
    {
        UpdateStatus(EDefragStatus::Scheduled);
    }
    
    if (ThreadEvent && !bInProgress && bThreadedDefragmentation)
    {
        ThreadEvent->Trigger();
    }
    
    return true;
}

bool FMemoryDefragmenter::ScheduleDefragmentationForAllPools(EDefragPriority Priority, float MaxTimeMs)
{
    if (!MemoryManager)
    {
        return false;
    }
    
    TArray<FName> PoolNames = MemoryManager->GetPoolNames();
    
    bool bSuccess = true;
    for (const FName& PoolName : PoolNames)
    {
        bSuccess &= ScheduleDefragmentation(PoolName, Priority, MaxTimeMs);
    }
    
    return bSuccess;
}

bool FMemoryDefragmenter::Init()
{
    return true;
}

uint32 FMemoryDefragmenter::Run()
{
    while (!bShouldStop)
    {
        {
            FScopeLock Lock(&DefragLock);
            
            if (DefragQueue.IsEmpty() || bIsPaused)
            {
                if (bInProgress)
                {
                    bInProgress = false;
                    UpdateStatus(EDefragStatus::Idle);
                }
            }
            else if (!bInProgress)
            {
                bInProgress = true;
                UpdateStatus(EDefragStatus::InProgress);
            }
        }
        
        if (bInProgress && !bIsPaused)
        {
            TPair<FName, TPair<EDefragPriority, float>> QueueItem;
            bool bHasItem = false;
            
            {
                FScopeLock Lock(&DefragLock);
                bHasItem = DefragQueue.Dequeue(QueueItem);
            }
            
            if (bHasItem)
            {
                const FName& PoolName = QueueItem.Key;
                const float MaxTimeMs = QueueItem.Value.Value;
                
                ProcessPoolDefragmentation(PoolName, MaxTimeMs);
            }
        }
        
        // Wait for event or timeout
        if (ThreadEvent && !bInProgress)
        {
            ThreadEvent->Wait(100); // 100ms timeout
        }
        else
        {
            FPlatformProcess::Sleep(0.01f); // 10ms sleep during active defrag
        }
    }
    
    return 0;
}

void FMemoryDefragmenter::Stop()
{
    bShouldStop = true;
    if (ThreadEvent)
    {
        ThreadEvent->Trigger();
    }
}

void FMemoryDefragmenter::Exit()
{
    // Cleanup if needed
}

bool FMemoryDefragmenter::DefragmentSynchronous(const FName& PoolName, float MaxTimeMs)
{
    FScopeLock Lock(&DefragLock);
    
    if (bInProgress && bThreadedDefragmentation)
    {
        return false; // Don't allow synchronous defrag when threaded defrag is active
    }
    
    // Store current state
    const bool bWasInProgress = bInProgress;
    const EDefragStatus OldStatus = Status;
    
    // Update state for synchronous operation
    bInProgress = true;
    UpdateStatus(EDefragStatus::InProgress);
    
    // Process defragmentation
    const bool bResult = ProcessPoolDefragmentation(PoolName, MaxTimeMs);
    
    // Restore previous state
    bInProgress = bWasInProgress;
    UpdateStatus(OldStatus);
    
    return bResult;
}

bool FMemoryDefragmenter::DefragmentAllPoolsSynchronous(float MaxTimeMs)
{
    if (!MemoryManager)
    {
        return false;
    }
    
    TArray<FName> PoolNames = MemoryManager->GetPoolNames();
    
    // Sort pools by fragmentation level
    struct FPoolFragInfo
    {
        FName PoolName;
        float FragmentationPercent;
    };
    
    TArray<FPoolFragInfo> PoolsToDefrag;
    for (const FName& PoolName : PoolNames)
    {
        float FragmentationPercent = 0.0f;
        uint64 LargestFreeBlockSize = 0;
        
        if (GetPoolFragmentationMetrics(PoolName, FragmentationPercent, LargestFreeBlockSize))
        {
            FPoolFragInfo Info;
            Info.PoolName = PoolName;
            Info.FragmentationPercent = FragmentationPercent;
            PoolsToDefrag.Add(Info);
        }
    }
    
    // Sort by fragmentation percentage (highest first)
    PoolsToDefrag.Sort([](const FPoolFragInfo& A, const FPoolFragInfo& B) {
        return A.FragmentationPercent > B.FragmentationPercent;
    });
    
    // Defragment pools in order of fragmentation
    bool bSuccess = true;
    const double StartTime = FPlatformTime::Seconds();
    double TotalTimeMs = 0.0;
    
    for (const FPoolFragInfo& Info : PoolsToDefrag)
    {
        // Check remaining time
        const double CurrentTime = FPlatformTime::Seconds();
        TotalTimeMs = (CurrentTime - StartTime) * 1000.0;
        
        if (TotalTimeMs >= MaxTimeMs)
        {
            break; // Time budget exceeded
        }
        
        // Allocate remaining time for this pool
        const float RemainingTimeMs = static_cast<float>(MaxTimeMs - TotalTimeMs);
        
        if (RemainingTimeMs <= 0.0f)
        {
            break;
        }
        
        // Skip pools with low fragmentation
        if (Info.FragmentationPercent < FragmentationThreshold)
        {
            continue;
        }
        
        bSuccess &= DefragmentSynchronous(Info.PoolName, RemainingTimeMs);
    }
    
    return bSuccess;
}

bool FMemoryDefragmenter::PauseDefragmentation()
{
    FScopeLock Lock(&DefragLock);
    
    if (!bInProgress)
    {
        return false;
    }
    
    bIsPaused = true;
    UpdateStatus(EDefragStatus::Paused);
    return true;
}

bool FMemoryDefragmenter::ResumeDefragmentation()
{
    FScopeLock Lock(&DefragLock);
    
    if (!bIsPaused)
    {
        return false;
    }
    
    bIsPaused = false;
    UpdateStatus(EDefragStatus::InProgress);
    
    if (ThreadEvent && bThreadedDefragmentation)
    {
        ThreadEvent->Trigger();
    }
    
    return true;
}

bool FMemoryDefragmenter::CancelDefragmentation()
{
    FScopeLock Lock(&DefragLock);
    
    if (!bInProgress && DefragQueue.IsEmpty())
    {
        return false;
    }
    
    // Clear the queue
    DefragQueue.Empty();
    
    if (bInProgress)
    {
        bIsPaused = true; // Pause if in progress
        UpdateStatus(EDefragStatus::Paused);
    }
    else
    {
        UpdateStatus(EDefragStatus::Idle);
    }
    
    return true;
}

void FMemoryDefragmenter::GetDefragmentationStats(FDefragStats& OutStats) const
{
    FScopeLock Lock(&DefragLock);
    OutStats = Stats;
}

EDefragStatus FMemoryDefragmenter::GetDefragmentationStatus() const
{
    FScopeLock Lock(&DefragLock);
    return Status;
}

bool FMemoryDefragmenter::RegisterAllocationReferences(void* Ptr, const TArray<void*>& ReferencesTo)
{
    if (!Ptr)
    {
        return false;
    }
    
    FScopeLock Lock(&ReferenceLock);
    
    // Register each reference
    for (void* ReferencedPtr : ReferencesTo)
    {
        if (!ReferencedPtr)
        {
            continue;
        }
        
        // Add to allocation references map
        TArray<void*>& References = AllocationReferences.FindOrAdd(Ptr);
        References.AddUnique(ReferencedPtr);
        
        // Add to reverse reference map
        TArray<void*>& ReverseRefs = ReferenceToAllocations.FindOrAdd(ReferencedPtr);
        ReverseRefs.AddUnique(Ptr);
    }
    
    return true;
}

uint32 FMemoryDefragmenter::UpdateReferences(void* OldPtr, void* NewPtr)
{
    if (!OldPtr || !NewPtr || OldPtr == NewPtr)
    {
        return 0;
    }
    
    FScopeLock Lock(&ReferenceLock);
    
    uint32 UpdatedCount = 0;
    
    // Get allocations that reference the moved memory
    TArray<void*>* RefList = ReferenceToAllocations.Find(OldPtr);
    if (RefList)
    {
        for (void* ReferencingPtr : *RefList)
        {
            // Find all references in this allocation
            TArray<void*>* References = AllocationReferences.Find(ReferencingPtr);
            if (References)
            {
                for (int32 i = 0; i < References->Num(); ++i)
                {
                    if ((*References)[i] == OldPtr)
                    {
                        // Update the reference to the new pointer
                        (*References)[i] = NewPtr;
                        UpdatedCount++;
                        
                        // Update the allocation directly if possible
                        // This should be handled by the memory manager implementation
                        if (MemoryManager)
                        {
                            MemoryManager->UpdatePointerReference(ReferencingPtr, OldPtr, (uint64)sizeof(void*));
                        }
                    }
                }
            }
        }
        
        // Update the reverse reference map
        TArray<void*> OldRefs = *RefList;
        ReferenceToAllocations.Remove(OldPtr);
        TArray<void*>& NewRefs = ReferenceToAllocations.FindOrAdd(NewPtr);
        
        for (void* Ref : OldRefs)
        {
            NewRefs.AddUnique(Ref);
        }
    }
    
    // Transfer references from the old pointer to the new pointer
    TArray<void*>* OldRefs = AllocationReferences.Find(OldPtr);
    if (OldRefs)
    {
        TArray<void*>& NewRefs = AllocationReferences.FindOrAdd(NewPtr);
        
        for (void* Ref : *OldRefs)
        {
            NewRefs.AddUnique(Ref);
        }
        
        AllocationReferences.Remove(OldPtr);
    }
    
    return UpdatedCount;
}

bool FMemoryDefragmenter::UnregisterAllocationReferences(void* Ptr)
{
    if (!Ptr)
    {
        return false;
    }
    
    FScopeLock Lock(&ReferenceLock);
    
    // Get references from this allocation
    TArray<void*>* References = AllocationReferences.Find(Ptr);
    if (References)
    {
        // Remove this pointer from the reverse lookup for all referenced pointers
        for (void* ReferencedPtr : *References)
        {
            TArray<void*>* ReverseRefs = ReferenceToAllocations.Find(ReferencedPtr);
            if (ReverseRefs)
            {
                ReverseRefs->Remove(Ptr);
                
                // Clean up empty reverse reference entries
                if (ReverseRefs->Num() == 0)
                {
                    ReferenceToAllocations.Remove(ReferencedPtr);
                }
            }
        }
        
        // Remove from the allocation references map
        AllocationReferences.Remove(Ptr);
    }
    
    // Remove from reverse reference map
    TArray<void*>* ReverseRefs = ReferenceToAllocations.Find(Ptr);
    if (ReverseRefs)
    {
        // Remove this pointer from the references of all allocations that reference it
        for (void* ReferencingPtr : *ReverseRefs)
        {
            TArray<void*>* ReferencingAlloc = AllocationReferences.Find(ReferencingPtr);
            if (ReferencingAlloc)
            {
                ReferencingAlloc->Remove(Ptr);
            }
        }
        
        // Remove from the reverse reference map
        ReferenceToAllocations.Remove(Ptr);
    }
    
    return true;
}

bool FMemoryDefragmenter::GetPoolFragmentationMetrics(const FName& PoolName, float& OutFragmentationPercent, uint64& OutLargestFreeBlockSize) const
{
    if (!MemoryManager || PoolName.IsNone())
    {
        OutFragmentationPercent = 0.0f;
        OutLargestFreeBlockSize = 0;
        return false;
    }
    
    // Get the pool allocator from the memory manager
    IPoolAllocator* Pool = MemoryManager->GetPool(PoolName);
    if (!Pool)
    {
        OutFragmentationPercent = 0.0f;
        OutLargestFreeBlockSize = 0;
        return false;
    }
    
    return CalculateFragmentationMetrics(Pool, OutFragmentationPercent, OutLargestFreeBlockSize);
}

void FMemoryDefragmenter::SetDefragmentationThreshold(float ThresholdPercent)
{
    FragmentationThreshold = FMath::Clamp(ThresholdPercent, 0.0f, 100.0f);
}

float FMemoryDefragmenter::GetDefragmentationThreshold() const
{
    return FragmentationThreshold;
}

void FMemoryDefragmenter::SetAutoDefragmentationEnabled(bool bEnable)
{
    bAutoDefragEnabled = bEnable;
}

bool FMemoryDefragmenter::IsAutoDefragmentationEnabled() const
{
    return bAutoDefragEnabled;
}

void FMemoryDefragmenter::SetThreadedDefragmentation(bool bInThreaded)
{
    if (bInThreaded == bThreadedDefragmentation)
    {
        return;
    }
    
    Shutdown();
    bThreadedDefragmentation = bInThreaded;
    Initialize();
}

bool FMemoryDefragmenter::IsThreadedDefragmentation() const
{
    return bThreadedDefragmentation;
}

bool FMemoryDefragmenter::ProcessPoolDefragmentation(const FName& PoolName, float MaxTimeMs)
{
    if (!MemoryManager || PoolName.IsNone() || MaxTimeMs <= 0.0f)
    {
        return false;
    }
    
    IPoolAllocator* Pool = MemoryManager->GetPool(PoolName);
    if (!Pool)
    {
        return false;
    }
    
    // Get starting fragmentation metrics
    float StartingFragmentation = 0.0f;
    uint64 StartingLargestBlock = 0;
    if (!CalculateFragmentationMetrics(Pool, StartingFragmentation, StartingLargestBlock))
    {
        return false;
    }
    
    // If fragmentation is below threshold, don't bother defragmenting
    if (StartingFragmentation < FragmentationThreshold)
    {
        return true;
    }
    
    const double StartTime = FPlatformTime::Seconds();
    double CurrentTime = StartTime;
    uint64 BytesMoved = 0;
    uint32 AllocsMoved = 0;
    uint32 ReferencesUpdated = 0;
    
    // Perform incremental defragmentation while time allows
    while ((CurrentTime - StartTime) * 1000.0 < MaxTimeMs)
    {
        // Check if we should stop
        if (bShouldStop || bIsPaused)
        {
            break;
        }
        
        // Perform a single defragmentation step
        void* OldPtr = nullptr;
        void* NewPtr = nullptr;
        uint64 AllocationSize = 0;
        
        // Get next allocation to move from the pool
        bool bMoved = Pool->MoveNextFragmentedAllocation(OldPtr, NewPtr, AllocationSize);
        
        if (!bMoved || !OldPtr || !NewPtr)
        {
            break; // No more allocations to move
        }
        
        // Update all references to this allocation
        uint32 Updated = UpdateReferences(OldPtr, NewPtr);
        
        BytesMoved += AllocationSize;
        AllocsMoved++;
        ReferencesUpdated += Updated;
        
        // Check time after each move
        CurrentTime = FPlatformTime::Seconds();
    }
    
    // Calculate time spent
    double TimeSpentMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    
    // Update statistics
    FScopeLock Lock(&DefragLock);
    
    // Get ending fragmentation metrics
    float EndingFragmentation = 0.0f;
    uint64 EndingLargestBlock = 0;
    CalculateFragmentationMetrics(Pool, EndingFragmentation, EndingLargestBlock);
    
    UpdateDefragStats(Pool, TimeSpentMs, BytesMoved);
    
    // Update specific stats for this run
    Stats.AllocsMoved += AllocsMoved;
    Stats.ReferencesUpdated += ReferencesUpdated;
    Stats.BytesMoved += BytesMoved;
    Stats.PassesCompleted++;
    Stats.MaxPassTimeMs = FMath::Max(Stats.MaxPassTimeMs, TimeSpentMs);
    
    // Calculate memory recovered
    if (EndingLargestBlock > StartingLargestBlock)
    {
        Stats.MemoryRecovered += (EndingLargestBlock - StartingLargestBlock);
    }
    
    // Success if we moved at least one allocation
    return (AllocsMoved > 0);
}

bool FMemoryDefragmenter::CalculateFragmentationMetrics(IPoolAllocator* Pool, float& OutFragmentationPercent, uint64& OutLargestFreeBlockSize) const
{
    if (!Pool)
    {
        OutFragmentationPercent = 0.0f;
        OutLargestFreeBlockSize = 0;
        return false;
    }
    
    // Get pool stats
    FPoolStats PoolStats = Pool->GetStats();
    
    // Calculate fragmentation percentage
    OutFragmentationPercent = PoolStats.FragmentationPercent;
    
    // Get the largest free block size from pool stats
    uint64 TotalSize = PoolStats.BlockSize * PoolStats.BlockCount;
    uint64 UsedSize = PoolStats.BlockSize * PoolStats.AllocatedBlocks;
    uint64 FreeSize = TotalSize - UsedSize;
    
    // Estimate largest free block based on fragmentation
    // If fragmentation is low, most free space is contiguous
    if (PoolStats.FragmentationPercent < 10.0f && FreeSize > 0)
    {
        OutLargestFreeBlockSize = FreeSize;
    }
    else if (PoolStats.FreeBlocks > 0)
    {
        // Estimate largest block as a portion of free space based on fragmentation
        float FragmentationRatio = PoolStats.FragmentationPercent / 100.0f;
        OutLargestFreeBlockSize = static_cast<uint64>(FreeSize * (1.0f - FragmentationRatio));
    }
    else
    {
        OutLargestFreeBlockSize = 0;
    }
    
    return true;
}

void FMemoryDefragmenter::UpdateStatus(EDefragStatus NewStatus)
{
    Status = NewStatus;
    Stats.bInProgress = (Status == EDefragStatus::InProgress);
}

void FMemoryDefragmenter::UpdateDefragStats(IPoolAllocator* Pool, double TimeSpentMs, uint64 BytesMoved)
{
    if (!Pool)
    {
        return;
    }
    
    Stats.TotalTimeMs += TimeSpentMs;
    
    // Calculate current fragmentation
    float FragmentationPercent = 0.0f;
    uint64 LargestFreeBlockSize = 0;
    if (CalculateFragmentationMetrics(Pool, FragmentationPercent, LargestFreeBlockSize))
    {
        Stats.FragmentationPercentage = FragmentationPercent;
    }
}

bool FMemoryDefragmenter::RegisterVersionedType(uint32 TypeId)
{
    FScopeLock Lock(&DefragLock);
    
    if (!MemoryManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryDefragmenter::RegisterVersionedType - No memory manager available"));
        return false;
    }
    
    // Check if the type exists in the memory manager
    IPoolAllocator* Pool = MemoryManager->GetPoolForType(TypeId);
    if (!Pool)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryDefragmenter::RegisterVersionedType - No pool found for type %u"), TypeId);
        return false;
    }
    
    // Store type ID for version tracking during defragmentation
    if (!VersionedTypes.Contains(TypeId))
    {
        UE_LOG(LogTemp, Verbose, TEXT("FMemoryDefragmenter::RegisterVersionedType - Registered type %u for version tracking"), TypeId);
        VersionedTypes.Add(TypeId);
    }
    
    return true;
}