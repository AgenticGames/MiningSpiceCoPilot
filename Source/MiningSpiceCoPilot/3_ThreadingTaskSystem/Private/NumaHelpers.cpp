// Copyright Epic Games, Inc. All Rights Reserved.

#include "NumaHelpers.h"
// Include the CoreMinimal.h after NumaHelpers.h
#include "CoreMinimal.h"
#include "ThreadSafety.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformAffinity.h"
#include "Math/UnrealMathUtility.h"

// On Windows, we need specific Windows APIs for NUMA functions
#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <WinBase.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace NumaHelpers
{
    uint32 GetNumberOfCoresPerProcessor()
    {
        // Default implementation - platform specific code may vary
        static uint32 CoresPerProcessor = 0;
        
        if (CoresPerProcessor == 0)
        {
            uint32 TotalCores = FPlatformMisc::NumberOfCores();
            uint32 NumNodes = 1; // Default to 1 if NUMA info not available
            
            // This is a simplification - on Windows we would use actual NUMA API
            // For now just estimate as 1/4 of cores per node on higher core count systems
            if (TotalCores >= 16)
            {
                NumNodes = 4;
            }
            else if (TotalCores >= 8)
            {
                NumNodes = 2;
            }
            
            CoresPerProcessor = FMath::Max(1u, TotalCores / NumNodes);
        }
        
        return CoresPerProcessor;
    }
    
    uint64 GetProcessorMaskForDomain(uint32 DomainId)
    {
        // Default implementation - ideally would use Windows or Linux NUMA API
        uint32 TotalCores = FPlatformMisc::NumberOfCores();
        uint32 CoresPerProcessor = GetNumberOfCoresPerProcessor();
        uint32 NumNodes = FMath::Max(1u, TotalCores / CoresPerProcessor);
        
        // Validate domain ID
        if (DomainId >= NumNodes)
        {
            return 0;
        }
        
        // Create a mask for the cores in this domain
        uint64 Mask = 0;
        uint32 StartCore = DomainId * CoresPerProcessor;
        uint32 EndCore = FMath::Min(TotalCores, (DomainId + 1) * CoresPerProcessor);
        
        for (uint32 CoreId = StartCore; CoreId < EndCore; ++CoreId)
        {
            Mask |= (1ULL << CoreId);
        }
        
        return Mask;
    }
    
    bool SetProcessorAffinityMask(uint64 ProcessorMask)
    {
#if PLATFORM_WINDOWS
        HANDLE ThreadHandle = GetCurrentThread();
        DWORD_PTR Result = SetThreadAffinityMask(ThreadHandle, static_cast<DWORD_PTR>(ProcessorMask));
        return Result != 0;
#else
        // Default implementation for other platforms
        return false;
#endif
    }
    
    uint32 GetDomainForAddress(void* Address)
    {
        // Default implementation - would use VirtualQueryEx on Windows with NUMA info
        // or move_pages on Linux
        
        // For now, just return domain 0
        return 0;
    }
    
    void* AllocateMemoryOnDomain(SIZE_T Size, uint32 DomainId)
    {
#if PLATFORM_WINDOWS
        // On Windows, would use VirtualAllocExNuma or similar
        // For now, just use regular allocation
#endif
        return FMemory::Malloc(Size);
    }
    
    void FreeNUMAMemory(void* Ptr)
    {
        FMemory::Free(Ptr);
    }
    
    uint64 GetAllCoresMask()
    {
        uint64 AllCoresMask = 0;
        
#if PLATFORM_WINDOWS
        // Try to get the system affinity mask first
        DWORD_PTR SystemMask = 0;
        DWORD_PTR ProcessMask = 0;
        if (::GetProcessAffinityMask(::GetCurrentProcess(), &ProcessMask, &SystemMask))
        {
            return (uint64)SystemMask;
        }
#endif
        
        // Fallback: Create a mask for all logical cores
        int32 NumLogicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
        for (int32 CoreIdx = 0; CoreIdx < NumLogicalCores; ++CoreIdx)
        {
            AllCoresMask |= (1ULL << CoreIdx);
        }
        
        return AllCoresMask;
    }
} 