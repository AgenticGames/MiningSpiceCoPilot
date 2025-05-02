#pragma once

#include "CoreMinimal.h"
#include "../Public/ComputeOperationTypes.h"
#include "../Public/GPUDispatcherLogging.h"

/**
 * Forward declaration for FSimulatedGPUFence
 */
class FSimulatedGPUFence;

/**
 * Simplified GPU buffer without RHI dependencies
 */
class FSimulatedGPUBuffer : public FSimplifiedResource
{
public:
    FSimulatedGPUBuffer(SIZE_T InSize, uint32 InUsageFlags, const FString& InName = TEXT(""))
        : Size(InSize)
        , UsageFlags(InUsageFlags)
        , Name(InName.IsEmpty() ? FString::Printf(TEXT("Buffer_%llu"), GetId()) : InName)
        , bInitialized(false)
    {
        // Allocate memory to simulate a GPU buffer
        Data = FMemory::Malloc(Size, 16);
        if (Data)
        {
            bInitialized = true;
            GPU_DISPATCHER_LOG_VERBOSE("Created simulated GPU buffer %s: Size=%llu, UsageFlags=0x%X", 
                *Name, Size, UsageFlags);
        }
        else
        {
            GPU_DISPATCHER_LOG_ERROR("Failed to create simulated GPU buffer %s: Size=%llu", 
                *Name, Size);
        }
    }
    
    virtual ~FSimulatedGPUBuffer()
    {
        if (Data)
        {
            FMemory::Free(Data);
            Data = nullptr;
            GPU_DISPATCHER_LOG_VERBOSE("Destroyed simulated GPU buffer %s", *Name);
        }
    }
    
    // Overrides from FSimplifiedResource
    virtual FString GetTypeName() const override { return TEXT("SimulatedGPUBuffer"); }
    virtual uint64 GetSizeBytes() const override { return Size; }
    
    // Simulated buffer operations
    void* GetCPUAddress() const { return Data; }
    
    SIZE_T GetSize() const { return Size; }
    
    uint32 GetUsageFlags() const { return UsageFlags; }
    
    const FString& GetName() const { return Name; }
    
    bool IsInitialized() const { return bInitialized; }
    
    void CopyData(const void* SrcData, SIZE_T DataSize, SIZE_T DstOffset = 0)
    {
        if (!Data || !bInitialized || DstOffset + DataSize > Size)
        {
            GPU_DISPATCHER_LOG_ERROR("Invalid buffer copy to %s: Size=%llu, DstOffset=%llu, CopySize=%llu", 
                *Name, Size, DstOffset, DataSize);
            return;
        }
        
        FMemory::Memcpy(static_cast<uint8*>(Data) + DstOffset, SrcData, DataSize);
        GPU_DISPATCHER_LOG_VERBOSE("Copied %llu bytes to buffer %s at offset %llu", 
            DataSize, *Name, DstOffset);
    }
    
private:
    SIZE_T Size;
    uint32 UsageFlags;
    FString Name;
    void* Data;
    bool bInitialized;
};

/**
 * Simulated GPU readback buffer without RHI dependencies
 */
class FSimulatedGPUReadback
{
public:
    FSimulatedGPUReadback(const TCHAR* InName)
        : Name(InName)
        , Buffer(nullptr)
        , Size(0)
        , bHasData(false)
    {
        GPU_DISPATCHER_LOG_VERBOSE("Created simulated GPU readback %s", *Name);
    }
    
    ~FSimulatedGPUReadback()
    {
        if (Buffer)
        {
            FMemory::Free(Buffer);
            Buffer = nullptr;
        }
        
        GPU_DISPATCHER_LOG_VERBOSE("Destroyed simulated GPU readback %s", *Name);
    }
    
    // Initialize with a given size
    bool Initialize(SIZE_T InSize)
    {
        if (Buffer)
        {
            FMemory::Free(Buffer);
        }
        
        Size = InSize;
        Buffer = FMemory::Malloc(Size, 16);
        bHasData = false;
        
        if (Buffer)
        {
            GPU_DISPATCHER_LOG_VERBOSE("Initialized simulated GPU readback %s: Size=%llu", 
                *Name, Size);
            return true;
        }
        else
        {
            GPU_DISPATCHER_LOG_ERROR("Failed to initialize simulated GPU readback %s: Size=%llu", 
                *Name, Size);
            return false;
        }
    }
    
    // Enqueue a readback operation (in a real implementation, this would be async)
    void EnqueueCopy(const void* SrcData, SIZE_T InSize)
    {
        if (!Buffer || InSize > Size)
        {
            // Resize if needed
            if (InSize > Size)
            {
                if (Buffer)
                {
                    FMemory::Free(Buffer);
                }
                
                Size = InSize;
                Buffer = FMemory::Malloc(Size, 16);
                
                if (!Buffer)
                {
                    GPU_DISPATCHER_LOG_ERROR("Failed to resize simulated GPU readback %s: Size=%llu", 
                        *Name, Size);
                    return;
                }
            }
        }
        
        if (Buffer && SrcData)
        {
            FMemory::Memcpy(Buffer, SrcData, InSize);
            bHasData = true;
            GPU_DISPATCHER_LOG_VERBOSE("Copied %llu bytes to simulated GPU readback %s", 
                InSize, *Name);
        }
    }
    
    // Check if data is ready (in a real implementation, this would be async)
    bool IsReady() const
    {
        return bHasData;
    }
    
    // Get the CPU address for readback data
    void* GetCPUAddress() const
    {
        return Buffer;
    }
    
    // Get the size of the readback buffer
    SIZE_T GetSize() const
    {
        return Size;
    }
    
    // Get the name of the readback buffer
    const FString& GetName() const
    {
        return Name;
    }
    
private:
    FString Name;
    void* Buffer;
    SIZE_T Size;
    bool bHasData;
};

/**
 * Simulated GPU fence for synchronization without RHI dependencies
 * Provides a mechanism for tracking GPU work completion
 */
class FSimulatedGPUFence
{
public:
    FSimulatedGPUFence(const TCHAR* InName = TEXT("SimulatedFence"))
        : Name(InName)
        , bSignaled(false)
        , CompletionTime(0.0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Created simulated GPU fence: %s"), *Name);
    }

    ~FSimulatedGPUFence()
    {
        UE_LOG(LogTemp, Verbose, TEXT("Destroyed simulated GPU fence: %s"), *Name);
    }

    // Signal the fence (simulates GPU work completion)
    void Signal()
    {
        bSignaled = true;
        CompletionTime = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Verbose, TEXT("Signaled GPU fence: %s"), *Name);
    }

    // Check if the fence has been signaled
    bool IsSignaled() const
    {
        return bSignaled;
    }
    
    // Poll method (alias for IsSignaled for compatibility)
    bool Poll() const
    {
        return IsSignaled();
    }

    // Wait for the fence to be signaled (with timeout)
    bool Wait(double TimeoutMs = 1000.0)
    {
        if (bSignaled)
        {
            return true;
        }

        const double StartTime = FPlatformTime::Seconds();
        const double EndTime = StartTime + (TimeoutMs / 1000.0);

        // Simulate fence waiting with a yield loop
        while (!bSignaled && FPlatformTime::Seconds() < EndTime)
        {
            // Yield to other threads
            FPlatformProcess::SleepNoStats(0.001f);
        }

        if (bSignaled)
        {
            UE_LOG(LogTemp, Verbose, TEXT("Wait completed for GPU fence: %s (%.2f ms)"), 
                *Name, (FPlatformTime::Seconds() - StartTime) * 1000.0);
            return true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Wait timed out for GPU fence: %s (%.2f ms)"), 
                *Name, TimeoutMs);
            return false;
        }
    }

    // Reset the fence for reuse
    void Reset()
    {
        bSignaled = false;
        CompletionTime = 0.0;
        UE_LOG(LogTemp, Verbose, TEXT("Reset GPU fence: %s"), *Name);
    }

    // Get the fence name
    const FString& GetName() const
    {
        return Name;
    }

    // Get the completion time (in seconds)
    double GetCompletionTime() const
    {
        return CompletionTime;
    }

private:
    FString Name;
    bool bSignaled;
    double CompletionTime;
};