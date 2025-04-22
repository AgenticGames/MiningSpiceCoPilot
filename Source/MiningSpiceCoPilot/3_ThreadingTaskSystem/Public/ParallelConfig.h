// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Parallel execution mode for the executor
 */
enum class EParallelExecutionMode : uint8
{
    /** Automatically determine whether to execute in parallel based on workload */
    Automatic,
    
    /** Force parallel execution even for small workloads */
    ForceParallel,
    
    /** Force sequential execution even for large workloads */
    ForceSequential
};

/**
 * Optimization strategy for parallel execution
 */
enum class EParallelOptimizationStrategy : uint8
{
    /** Default with minimal optimizations */
    Default,
    
    /** SIMD optimized strategy */
    SIMDOptimized,
    
    /** Cache-friendly strategy */
    CacheOptimized,
    
    /** Adaptive strategy that changes based on workload */
    Adaptive
};

/**
 * Configuration structure for parallel operations
 */
struct FParallelConfig
{
    /** Execution mode (auto, force parallel, or force sequential) */
    EParallelExecutionMode ExecutionMode;
    
    /** Granularity (items per work chunk) - 0 for automatic */
    int32 Granularity;
    
    /** Whether to use work stealing for load balancing */
    bool bUseWorkStealing;
    
    /** Whether to use thread affinity for cache coherence */
    bool bUseThreadAffinity;
    
    /** Whether to use SIMD intrinsics for vectorized operations */
    bool bUseIntrinsics;
    
    /** Optimization strategy to use */
    EParallelOptimizationStrategy OptimizationStrategy;
    
    /** Constructor with default values */
    FParallelConfig()
        : ExecutionMode(EParallelExecutionMode::Automatic)
        , Granularity(0)
        , bUseWorkStealing(true)
        , bUseThreadAffinity(false)
        , bUseIntrinsics(false)
        , OptimizationStrategy(EParallelOptimizationStrategy::Default)
    {
    }
    
    /**
     * Sets the execution mode
     * @param InMode Execution mode to use
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetExecutionMode(EParallelExecutionMode InMode)
    {
        ExecutionMode = InMode;
        return *this;
    }
    
    /**
     * Sets the granularity (items per work chunk)
     * @param InGranularity Granularity to use (0 for automatic)
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetGranularity(int32 InGranularity)
    {
        Granularity = InGranularity;
        return *this;
    }
    
    /**
     * Sets whether to use work stealing
     * @param bInUseWorkStealing Whether to use work stealing
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetWorkStealing(bool bInUseWorkStealing)
    {
        bUseWorkStealing = bInUseWorkStealing;
        return *this;
    }
    
    /**
     * Sets whether to use thread affinity
     * @param bInUseThreadAffinity Whether to use thread affinity
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetThreadAffinity(bool bInUseThreadAffinity)
    {
        bUseThreadAffinity = bInUseThreadAffinity;
        return *this;
    }
    
    /**
     * Sets whether to use SIMD intrinsics
     * @param bInUseIntrinsics Whether to use SIMD intrinsics
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetUseIntrinsics(bool bInUseIntrinsics)
    {
        bUseIntrinsics = bInUseIntrinsics;
        return *this;
    }
    
    /**
     * Sets the optimization strategy
     * @param InStrategy Optimization strategy to use
     * @return Reference to this config for chaining
     */
    FParallelConfig& SetOptimizationStrategy(EParallelOptimizationStrategy InStrategy)
    {
        OptimizationStrategy = InStrategy;
        return *this;
    }
}; 