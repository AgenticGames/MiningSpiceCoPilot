// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlatformMiscExtensions.h"
#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include <intrin.h>

// Implementation of SIMD support detection for FPlatformMiscExtensions
bool FPlatformMiscExtensions::SupportsSSE2()
{
    // SSE2 is available on all x64 processors and most modern x86 processors
    // For x64 builds, we can simply return true
#if PLATFORM_64BITS
    return true;
#else
    // For 32-bit, we need to check CPU features
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    
    // Check SSE2 bit (bit 26 in EDX)
    return (CPUInfo[3] & (1 << 26)) != 0;
#endif
}

bool FPlatformMiscExtensions::SupportsAVX()
{
    int CPUInfo[4];
    __cpuid(CPUInfo, 1);
    
    // Check OSXSAVE bit (bit 27 in ECX) to ensure OS support for XSAVE/XRSTOR
    bool OSSupportsXSAVE = (CPUInfo[2] & (1 << 27)) != 0;
    
    // Check AVX bit (bit 28 in ECX)
    bool CPUSupportsAVX = (CPUInfo[2] & (1 << 28)) != 0;
    
    if (OSSupportsXSAVE && CPUSupportsAVX)
    {
        // Check if the OS has enabled XMM and YMM state management
        unsigned long long xcrFeatureMask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        return (xcrFeatureMask & 0x6) == 0x6;
    }
    
    return false;
}

bool FPlatformMiscExtensions::SupportsAVX2()
{
    // First check AVX support, as AVX2 requires AVX
    if (!SupportsAVX())
    {
        return false;
    }
    
    // Check for AVX2 support
    int CPUInfo[4];
    __cpuid(CPUInfo, 0);
    
    // Check if CPUID supports extended features
    if (CPUInfo[0] >= 7)
    {
        __cpuidex(CPUInfo, 7, 0);
        
        // Check AVX2 bit (bit 5 in EBX)
        return (CPUInfo[1] & (1 << 5)) != 0;
    }
    
    return false;
}

// Implementation of GPU support detection
bool FPlatformMiscExtensions::SupportsRenderTarget()
{
    // Basic check for rendering capability
    // This is a simplified version - in a real implementation, you'd check
    // for actual GPU capabilities through platform-specific means
#if WITH_EDITOR || PLATFORM_DESKTOP
    return true;
#else
    return false;
#endif
}
#else
// Non-Windows platform implementation (default to false)
bool FPlatformMiscExtensions::SupportsSSE2()
{
    return false;
}

bool FPlatformMiscExtensions::SupportsAVX()
{
    return false;
}

bool FPlatformMiscExtensions::SupportsAVX2()
{
    return false;
}

bool FPlatformMiscExtensions::SupportsRenderTarget()
{
    return false;
}
#endif 