// Copyright Epic Games, Inc. All Rights Reserved.

// Include the CoreMinimal.h first to get all the basic includes
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
    int32 GetNumberOfCoresPerProcessor()
    {
        int32 TotalCores = FPlatformMisc::NumberOfCores();
        int32 TotalThreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
        int32 NumProcessors = (TotalThreads > TotalCores) ? (TotalThreads / TotalCores) : 1;
        return TotalCores / NumProcessors;
    }
    
    uint64 GetProcessorMaskForDomain(uint32 DomainId)
    {
        uint64 Mask = 0;
        
#if PLATFORM_WINDOWS
        ULONG HighestNodeNumber = 0;
        if (::GetNumaHighestNodeNumber(&HighestNodeNumber) && DomainId <= HighestNodeNumber)
        {
            ULONGLONG AvailableMask = 0;
            if (::GetNumaNodeProcessorMask((UCHAR)DomainId, &AvailableMask))
            {
                return (uint64)AvailableMask;
            }
        }
#endif
        
        // Fallback: Allocate cores evenly across domains
        int32 TotalCores = FPlatformMisc::NumberOfCores();
        int32 TotalThreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
        int32 NumDomains = (TotalThreads > TotalCores) ? (TotalThreads / TotalCores) : 1;
        
        int32 CoresPerDomain = TotalCores / NumDomains;
        int32 StartCore = DomainId * CoresPerDomain;
        int32 EndCore = FMath::Min(StartCore + CoresPerDomain, TotalCores);
        
        for (int32 CoreId = StartCore; CoreId < EndCore; ++CoreId)
        {
            Mask |= (1ULL << CoreId);
        }
        
        return Mask;
    }
    
    bool SetProcessorAffinityMask(uint64 AffinityMask)
    {
#if PLATFORM_WINDOWS
        HANDLE Process = ::GetCurrentProcess();
        return !!::SetProcessAffinityMask(Process, (DWORD_PTR)AffinityMask);
#else
        return false;
#endif
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