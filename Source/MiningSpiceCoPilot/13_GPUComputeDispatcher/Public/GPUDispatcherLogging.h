#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGPUDispatcher, Log, All);

// Performance logging macros for debug/profiling
#define GPU_DISPATCHER_LOG_PERF(Format, ...) UE_LOG(LogGPUDispatcher, Display, TEXT("[PERF] " Format), ##__VA_ARGS__)
#define GPU_DISPATCHER_LOG_PERF_VERBOSE(Format, ...) UE_LOG(LogGPUDispatcher, Verbose, TEXT("[PERF] " Format), ##__VA_ARGS__)

// Error logging macros
#define GPU_DISPATCHER_LOG_ERROR(Format, ...) UE_LOG(LogGPUDispatcher, Error, TEXT("[ERROR] " Format), ##__VA_ARGS__)
#define GPU_DISPATCHER_LOG_WARNING(Format, ...) UE_LOG(LogGPUDispatcher, Warning, TEXT("[WARNING] " Format), ##__VA_ARGS__)

// Debug logging macros
#define GPU_DISPATCHER_LOG_DEBUG(Format, ...) UE_LOG(LogGPUDispatcher, Log, TEXT("[DEBUG] " Format), ##__VA_ARGS__)
#define GPU_DISPATCHER_LOG_VERBOSE(Format, ...) UE_LOG(LogGPUDispatcher, Verbose, TEXT("[VERBOSE] " Format), ##__VA_ARGS__)

// Timing macros for performance measurement
#define GPU_DISPATCHER_SCOPED_TIMER(TimerName) \
    double ScopedStartTime_##TimerName = FPlatformTime::Seconds(); \
    const char* ScopedTimerName_##TimerName = #TimerName;

#define GPU_DISPATCHER_END_TIMER(TimerName) \
    double ScopedEndTime_##TimerName = FPlatformTime::Seconds(); \
    GPU_DISPATCHER_LOG_PERF("%s: %.4f ms", \
        ScopedTimerName_##TimerName, \
        (ScopedEndTime_##TimerName - ScopedStartTime_##TimerName) * 1000.0);