// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TaskTypes.generated.h"

// Forward declare the ETaskType enum used from ITaskScheduler.h
enum class ETaskType : uint8;

/**
 * Task status enumeration
 */
UENUM(BlueprintType)
enum class ETaskStatus : uint8
{
    /** Task is waiting in the queue */
    Queued UMETA(DisplayName = "Queued"),
    
    /** Task is currently executing */
    Executing UMETA(DisplayName = "Executing"),
    
    /** Task completed successfully */
    Completed UMETA(DisplayName = "Completed"),
    
    /** Task was cancelled before completion */
    Cancelled UMETA(DisplayName = "Cancelled"),
    
    /** Task failed during execution */
    Failed UMETA(DisplayName = "Failed"),
    
    /** Task is waiting for dependencies */
    Waiting UMETA(DisplayName = "Waiting for Dependencies"),
    
    /** Task is suspended and will resume later */
    Suspended UMETA(DisplayName = "Suspended")
};

/**
 * Task priority enumeration
 */
UENUM(BlueprintType)
enum class ETaskPriority : uint8
{
    /** Critical priority - processed before all others */
    Critical UMETA(DisplayName = "Critical"),
    
    /** High priority tasks */
    High UMETA(DisplayName = "High"),
    
    /** Normal priority tasks (default) */
    Normal UMETA(DisplayName = "Normal"),
    
    /** Low priority background tasks */
    Low UMETA(DisplayName = "Low"),
    
    /** Lowest priority - only run when system is idle */
    Background UMETA(DisplayName = "Background")
};

/**
 * Task type enumeration categorizing different processing types
 */
UENUM(BlueprintType)
enum class ETaskType : uint8
{
    /** General purpose task */
    General UMETA(DisplayName = "General"),
    
    /** Mining operation task with specific optimization */
    MiningOperation UMETA(DisplayName = "Mining Operation"),
    
    /** SDF field operation with SIMD optimization */
    SDFOperation UMETA(DisplayName = "SDF Operation"),
    
    /** Octree traversal operation with spatial coherence */
    OctreeTraversal UMETA(DisplayName = "Octree Traversal"),
    
    /** Material processing operation with channel awareness */
    MaterialOperation UMETA(DisplayName = "Material Operation"),
    
    /** Zone-based transaction task with concurrency control */
    ZoneTransaction UMETA(DisplayName = "Zone Transaction"),
    
    /** CPU-intensive computation task */
    Computation UMETA(DisplayName = "Computation"),
    
    /** I/O bound task */
    IO UMETA(DisplayName = "I/O"),
    
    /** Network task */
    Network UMETA(DisplayName = "Network"),
    
    /** Graphics or rendering task */
    Rendering UMETA(DisplayName = "Rendering"),
    
    /** Physics simulation task */
    Physics UMETA(DisplayName = "Physics"),
    
    /** Mining operation task */
    Mining UMETA(DisplayName = "Mining"),
    
    /** Data compression task */
    Compression UMETA(DisplayName = "Compression"),
    
    /** Memory management task */
    Memory UMETA(DisplayName = "Memory"),
    
    /** Maintenance or utility task */
    Maintenance UMETA(DisplayName = "Maintenance")
};