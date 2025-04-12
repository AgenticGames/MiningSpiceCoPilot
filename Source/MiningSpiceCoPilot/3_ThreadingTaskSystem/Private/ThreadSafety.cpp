// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafety.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadManager.h"

// Initialize static instance to nullptr
FThreadSafety* FThreadSafety::Instance = nullptr;

// Initialize thread-local storage for hierarchical lock
thread_local uint32 FHierarchicalLock::ThreadHighestLockLevel = 0;

//----------------------------------------------------------------------
// FMiningReaderWriterLock Implementation
//----------------------------------------------------------------------

FMiningReaderWriterLock::FMiningReaderWriterLock()
{
    WriterEvent = FPlatformProcess::GetSynchEventFromPool(false);
    ReaderEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FMiningReaderWriterLock::~FMiningReaderWriterLock()
{
    FPlatformProcess::ReturnSynchEventToPool(WriterEvent);
    FPlatformProcess::ReturnSynchEventToPool(ReaderEvent);
    WriterEvent = nullptr;
    ReaderEvent = nullptr;
}

bool FMiningReaderWriterLock::ReadLock(uint32 TimeoutMs)
{
    double StartTime = FPlatformTime::Seconds();
    double TimeoutSeconds = TimeoutMs / 1000.0;
    bool bTimedOut = false;
    
    while (true)
    {
        // Check if a writer is active
        if (WriterActive.GetValue() == 0)
        {
            // Increment reader count
            ReaderCount.Increment();
            
            // Double-check writer status after incrementing to avoid race condition
            // This is the key to avoiding read-write races
            if (WriterActive.GetValue() == 0)
            {
                // Successfully acquired read lock
                return true;
            }
            
            // Writer became active after we incremented reader count, back off
            ReaderCount.Decrement();
        }
        
        // A writer is active or waiting, wait for it to complete
        if (TimeoutMs > 0)
        {
            double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
            if (ElapsedSeconds >= TimeoutSeconds)
            {
                return false; // Timed out
            }
            
            uint32 WaitTimeMs = FMath::Min<uint32>(10, TimeoutMs - static_cast<uint32>(ElapsedSeconds * 1000));
            if (!ReaderEvent->Wait(WaitTimeMs * 1000)) // Convert to microseconds
            {
                // This can either mean timeout or spurious wakeup, check timeout
                continue;
            }
        }
        else
        {
            // No timeout, just yield to give writer a chance
            FPlatformProcess::Sleep(0.001f);
        }
    }
    
    return false;
}

void FMiningReaderWriterLock::ReadUnlock()
{
    int32 PreviousReaderCount = ReaderCount.Decrement();
    
    // If this was the last reader and a writer is waiting, signal the writer
    if (PreviousReaderCount == 1 && WriterWaiting.GetValue() > 0)
    {
        WriterEvent->Trigger();
    }
}

bool FMiningReaderWriterLock::WriteLock(uint32 TimeoutMs)
{
    double StartTime = FPlatformTime::Seconds();
    double TimeoutSeconds = TimeoutMs / 1000.0;
    
    // First, indicate that a writer is waiting
    WriterWaiting.Increment();
    
    while (true)
    {
        // Try to set the writer active flag if no other writer is active
        if (WriterActive.CompareExchange(1, 0) == 0)
        {
            // Writer active flag has been set, now wait for all readers to finish
            if (ReaderCount.GetValue() == 0)
            {
                // Successfully acquired write lock, store current thread ID
                WriterThreadId.Set(static_cast<int32>(GetCurrentThreadId()));
                WriterWaiting.Decrement();
                return true;
            }
            
            // Need to wait for readers to finish, first release writer active flag
            WriterActive.Set(0);
        }
        
        // Check timeout
        if (TimeoutMs > 0)
        {
            double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
            if (ElapsedSeconds >= TimeoutSeconds)
            {
                WriterWaiting.Decrement();
                return false; // Timed out
            }
            
            uint32 WaitTimeMs = FMath::Min<uint32>(10, TimeoutMs - static_cast<uint32>(ElapsedSeconds * 1000));
            WriterEvent->Wait(WaitTimeMs * 1000); // Convert to microseconds
        }
        else
        {
            // No timeout specified, sleep a bit to avoid burning CPU
            FPlatformProcess::Sleep(0.001f);
        }
    }
    
    WriterWaiting.Decrement();
    return false;
}

void FMiningReaderWriterLock::WriteUnlock()
{
    // Clear the writer thread ID
    WriterThreadId.Set(0);
    
    // Clear the writer active flag
    WriterActive.Set(0);
    
    // Signal any waiting readers
    ReaderEvent->Trigger();
}

bool FMiningReaderWriterLock::IsWriteLocked() const
{
    return WriterActive.GetValue() > 0;
}

int32 FMiningReaderWriterLock::GetReaderCount() const
{
    return ReaderCount.GetValue();
}

bool FMiningReaderWriterLock::IsWritePending() const
{
    return WriterWaiting.GetValue() > 0;
}

bool FMiningReaderWriterLock::TryUpgradeToWriteLock()
{
    // This can only be done if there's exactly one reader (us)
    if (ReaderCount.GetValue() != 1)
    {
        return false;
    }
    
    // Try to acquire write lock
    if (WriterActive.CompareExchange(1, 0) == 0)
    {
        // Successfully acquired write lock, store current thread ID
        WriterThreadId.Set(static_cast<int32>(GetCurrentThreadId()));
        
        // Decrement reader count as we're now a writer
        ReaderCount.Decrement();
        return true;
    }
    
    return false;
}

void FMiningReaderWriterLock::DowngradeToReadLock()
{
    if (WriterActive.GetValue() > 0)
    {
        // Increment reader count first
        ReaderCount.Increment();
        
        // Now release write lock
        WriterThreadId.Set(0);
        WriterActive.Set(0);
        
        // Signal any waiting readers/writers
        ReaderEvent->Trigger();
    }
}

uint32 FMiningReaderWriterLock::GetCurrentThreadId()
{
    return static_cast<uint32>(FPlatformTLS::GetCurrentThreadId());
}

//----------------------------------------------------------------------
// FScopedReadLock Implementation
//----------------------------------------------------------------------

FScopedReadLock::FScopedReadLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs)
    : Lock(InLock)
    , bLocked(false)
{
    bLocked = Lock.ReadLock(TimeoutMs);
}

FScopedReadLock::~FScopedReadLock()
{
    if (bLocked)
    {
        Lock.ReadUnlock();
        bLocked = false;
    }
}

bool FScopedReadLock::IsLocked() const
{
    return bLocked;
}

//----------------------------------------------------------------------
// FScopedWriteLock Implementation
//----------------------------------------------------------------------

FScopedWriteLock::FScopedWriteLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs)
    : Lock(InLock)
    , bLocked(false)
{
    bLocked = Lock.WriteLock(TimeoutMs);
}

FScopedWriteLock::~FScopedWriteLock()
{
    if (bLocked)
    {
        Lock.WriteUnlock();
        bLocked = false;
    }
}

bool FScopedWriteLock::IsLocked() const
{
    return bLocked;
}

//----------------------------------------------------------------------
// FHierarchicalLock Implementation
//----------------------------------------------------------------------

FHierarchicalLock::FHierarchicalLock(uint32 InLevel)
    : Level(InLevel)
{
    LockCount.Set(0);
    OwnerThreadId.Set(0);
}

FHierarchicalLock::~FHierarchicalLock()
{
    // Ensure the lock is released before destruction
    check(LockCount.GetValue() == 0);
}

bool FHierarchicalLock::Lock(uint32 TimeoutMs)
{
    // Check if this thread already has the lock
    uint32 CurrentThreadId = static_cast<uint32>(FPlatformTLS::GetCurrentThreadId());
    if (OwnerThreadId.GetValue() == static_cast<int32>(CurrentThreadId))
    {
        // Already owned by this thread, just increment lock count
        LockCount.Increment();
        return true;
    }
    
    // Verify hierarchical ordering - can only acquire locks with higher levels
    // than any locks we already hold
    if (Level <= ThreadHighestLockLevel)
    {
        // Violation of hierarchy - must acquire locks in ascending order
        UE_LOG(LogTemp, Error, TEXT("Hierarchical lock violation: Trying to acquire lock at level %u while holding lock at level %u"), 
            Level, ThreadHighestLockLevel);
        return false;
    }
    
    // Try to acquire the lock with timeout
    bool bLockAcquired = false;
    if (TimeoutMs > 0)
    {
        bLockAcquired = InternalLock.TryLock(TimeoutMs * 1000); // Convert to microseconds
    }
    else
    {
        InternalLock.Lock();
        bLockAcquired = true;
    }
    
    if (bLockAcquired)
    {
        // Set owner and count
        OwnerThreadId.Set(static_cast<int32>(CurrentThreadId));
        LockCount.Set(1);
        
        // Update thread's highest lock level
        ThreadHighestLockLevel = FMath::Max(ThreadHighestLockLevel, Level);
        
        return true;
    }
    
    return false;
}

void FHierarchicalLock::Unlock()
{
    // Verify this thread owns the lock
    uint32 CurrentThreadId = static_cast<uint32>(FPlatformTLS::GetCurrentThreadId());
    check(OwnerThreadId.GetValue() == static_cast<int32>(CurrentThreadId));
    
    // Decrement lock count
    int32 NewCount = LockCount.Decrement();
    
    // If this was the last lock, release it
    if (NewCount == 0)
    {
        // Clear owner
        OwnerThreadId.Set(0);
        
        // Reset thread's highest lock level if this was the highest
        if (ThreadHighestLockLevel == Level)
        {
            ThreadHighestLockLevel = 0;
        }
        
        // Release the internal lock
        InternalLock.Unlock();
    }
}

uint32 FHierarchicalLock::GetLevel() const
{
    return Level;
}

bool FHierarchicalLock::IsLockedByCurrentThread() const
{
    uint32 CurrentThreadId = static_cast<uint32>(FPlatformTLS::GetCurrentThreadId());
    return OwnerThreadId.GetValue() == static_cast<int32>(CurrentThreadId);
}

//----------------------------------------------------------------------
// FHybridLock Implementation
//----------------------------------------------------------------------

FHybridLock::FHybridLock()
    : ContentionThreshold(10)
{
    ContentionCount.Set(0);
    bUseSlowLock.Set(0);
}

FHybridLock::~FHybridLock()
{
    // Nothing to clean up
}

bool FHybridLock::Lock(uint32 TimeoutMs)
{
    // Use either fast or slow lock based on contention level
    if (bUseSlowLock.GetValue() == 0)
    {
        // Use spin lock for low contention
        if (FastLock.TryLock())
        {
            return true;
        }
        
        // Failed to acquire immediately, increment contention count
        UpdateContentionStats();
        
        // Try with timeout
        if (TimeoutMs > 0)
        {
            double StartTime = FPlatformTime::Seconds();
            double TimeoutSeconds = TimeoutMs / 1000.0;
            
            while (true)
            {
                if (FastLock.TryLock())
                {
                    return true;
                }
                
                double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
                if (ElapsedSeconds >= TimeoutSeconds)
                {
                    return false; // Timed out
                }
                
                // Sleep a bit before retrying
                FPlatformProcess::Sleep(0.001f);
            }
        }
        else
        {
            // No timeout, keep trying
            while (!FastLock.TryLock())
            {
                FPlatformProcess::Sleep(0.001f);
            }
            return true;
        }
    }
    else
    {
        // Use critical section for high contention
        if (TimeoutMs > 0)
        {
            return SlowLock.TryLock(TimeoutMs * 1000); // Convert to microseconds
        }
        else
        {
            SlowLock.Lock();
            return true;
        }
    }
    
    return false;
}

void FHybridLock::Unlock()
{
    if (bUseSlowLock.GetValue() == 0)
    {
        // Using spin lock
        FastLock.Unlock();
    }
    else
    {
        // Using critical section
        SlowLock.Unlock();
    }
}

uint32 FHybridLock::GetContentionCount() const
{
    return static_cast<uint32>(ContentionCount.GetValue());
}

void FHybridLock::ResetContentionStats()
{
    ContentionCount.Set(0);
    bUseSlowLock.Set(0);
}

void FHybridLock::SetContentionThreshold(uint32 Threshold)
{
    ContentionThreshold = Threshold;
}

void FHybridLock::UpdateContentionStats()
{
    int32 NewCount = ContentionCount.Increment();
    
    // Switch to slow lock if contention is high
    if (NewCount >= static_cast<int32>(ContentionThreshold) && bUseSlowLock.GetValue() == 0)
    {
        // Try to be the one to set the switch flag
        bUseSlowLock.CompareExchange(1, 0);
    }
}

//----------------------------------------------------------------------
// FWaitFreeCounter Implementation
//----------------------------------------------------------------------

FWaitFreeCounter::FWaitFreeCounter()
    : Counter(0)
{
}

FWaitFreeCounter::FWaitFreeCounter(int32 InitialValue)
    : Counter(InitialValue)
{
}

int32 FWaitFreeCounter::GetValue() const
{
    return Counter.load(std::memory_order_relaxed);
}

void FWaitFreeCounter::SetValue(int32 Value)
{
    Counter.store(Value, std::memory_order_release);
}

int32 FWaitFreeCounter::Increment()
{
    return Counter.fetch_add(1, std::memory_order_acq_rel) + 1;
}

int32 FWaitFreeCounter::Decrement()
{
    return Counter.fetch_sub(1, std::memory_order_acq_rel) - 1;
}

int32 FWaitFreeCounter::Add(int32 Amount)
{
    return Counter.fetch_add(Amount, std::memory_order_acq_rel) + Amount;
}

int32 FWaitFreeCounter::Exchange(int32 NewValue)
{
    return Counter.exchange(NewValue, std::memory_order_acq_rel);
}

bool FWaitFreeCounter::CompareExchange(int32& OutOldValue, int32 NewValue, int32 Comparand)
{
    OutOldValue = Comparand;
    return Counter.compare_exchange_strong(OutOldValue, NewValue, std::memory_order_acq_rel);
}

//----------------------------------------------------------------------
// FThreadSafety Implementation
//----------------------------------------------------------------------

FThreadSafety::FThreadSafety()
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FThreadSafety::~FThreadSafety()
{
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FThreadSafety& FThreadSafety::Get()
{
    check(Instance != nullptr);
    return *Instance;
}

FMiningReaderWriterLock* FThreadSafety::CreateReaderWriterLock()
{
    return new FMiningReaderWriterLock();
}

FHierarchicalLock* FThreadSafety::CreateHierarchicalLock(uint32 Level)
{
    return new FHierarchicalLock(Level);
}

FHybridLock* FThreadSafety::CreateHybridLock()
{
    return new FHybridLock();
}

FWaitFreeCounter* FThreadSafety::CreateWaitFreeCounter(int32 InitialValue)
{
    return new FWaitFreeCounter(InitialValue);
}

bool FThreadSafety::DetectPotentialDeadlock(const FString& LockName, uint32 TimeoutMs)
{
    return false;
}

TArray<uint32> FThreadSafety::GetThreadLockOrder()
{
    TArray<uint32> Result;
    Result.Add(FHierarchicalLock::ThreadHighestLockLevel);
    return Result;
}

bool FThreadSafety::ValidateThreadSafety(TFunction<void()> TestFunc, int32 ThreadCount, int32 IterationCount)
{
    if (!TestFunc || ThreadCount <= 0 || IterationCount <= 0)
    {
        return false;
    }
    
    // Set up validation state
    FValidationState State;
    State.TestFunction = TestFunc;
    State.ErrorCount.Set(0);
    
    // Create and start test threads
    TArray<FRunnableThread*> Threads;
    Threads.Reserve(ThreadCount);
    
    for (int32 i = 0; i < ThreadCount; ++i)
    {
        FString ThreadName = FString::Printf(TEXT("ThreadSafetyTest_%d"), i);
        FRunnableThread* Thread = FRunnableThread::Create(
            [&State, IterationCount]() -> uint32
            {
                for (int32 j = 0; j < IterationCount; ++j)
                {
                    // Execute the test function, catching any exceptions
                    try
                    {
                        State.TestFunction();
                    }
                    catch (...)
                    {
                        State.ErrorCount.Increment();
                    }
                }
                return 0;
            },
            *ThreadName);
        
        if (Thread)
        {
            Threads.Add(Thread);
        }
        else
        {
            // Failed to create thread
            State.ErrorCount.Increment();
        }
    }
    
    // Wait for threads to complete
    for (FRunnableThread* Thread : Threads)
    {
        if (Thread)
        {
            Thread->WaitForCompletion();
            delete Thread;
        }
    }
    Threads.Empty();
    
    // Return true if no errors were detected
    return State.ErrorCount.GetValue() == 0;
}

FString FThreadSafety::GenerateContentionReport(void* LockPtr, float MonitoringPeriodSeconds)
{
    if (!LockPtr || MonitoringPeriodSeconds <= 0.0f)
    {
        return TEXT("Invalid parameters for contention report");
    }
    
    // Get the thread safety instance
    FThreadSafety& SafetyUtils = FThreadSafety::Get();
    
    // Record initial contention stats
    uint32 InitialContentionCount = 0;
    {
        FScopeLock Lock(&SafetyUtils.ContentionStatsLock);
        InitialContentionCount = SafetyUtils.LockContentionStats.FindRef(LockPtr);
    }
    
    // Wait for the monitoring period
    FPlatformProcess::Sleep(MonitoringPeriodSeconds);
    
    // Record final contention stats
    uint32 FinalContentionCount = 0;
    {
        FScopeLock Lock(&SafetyUtils.ContentionStatsLock);
        FinalContentionCount = SafetyUtils.LockContentionStats.FindRef(LockPtr);
    }
    
    // Calculate contention rate
    uint32 ContentionCount = FinalContentionCount - InitialContentionCount;
    float ContentionRate = ContentionCount / MonitoringPeriodSeconds;
    
    // Generate the report
    FString Report = FString::Printf(
        TEXT("Contention Report for Lock at %p:\n")
        TEXT("Monitoring Period: %.2f seconds\n")
        TEXT("Contention Events: %u\n")
        TEXT("Contention Rate: %.2f events/second\n"),
        LockPtr, MonitoringPeriodSeconds, ContentionCount, ContentionRate);
    
    // Classify the contention level
    if (ContentionRate < 1.0f)
    {
        Report += TEXT("Contention Level: Low\n");
    }
    else if (ContentionRate < 10.0f)
    {
        Report += TEXT("Contention Level: Moderate\n");
    }
    else
    {
        Report += TEXT("Contention Level: High - Consider optimizing access patterns\n");
    }
    
    return Report;
}

uint32 FThreadSafety::ValidationThreadFunc(void* Params)
{
    FValidationState* State = static_cast<FValidationState*>(Params);
    if (!State || !State->TestFunction)
    {
        return 1;
    }
    
    try
    {
        State->TestFunction();
    }
    catch (...)
    {
        State->ErrorCount.Increment();
    }
    
    return 0;
}

void FThreadSafety::RecordContention(void* LockPtr)
{
    if (!LockPtr)
    {
        return;
    }
    
    FScopeLock Lock(&ContentionStatsLock);
    
    // Increment contention count for this lock
    uint32& ContentionCount = LockContentionStats.FindOrAdd(LockPtr);
    ContentionCount++;
}