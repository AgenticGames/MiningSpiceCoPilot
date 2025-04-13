// CrossRegionProcessor.h - System 25: SVO+SDF Hybrid Volume Representation System
// Handles continuity of geological features across region boundaries

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declarations
class IRegionManager;
class IMemoryManager;
class ITaskScheduler;
class ITransactionManager;
class UEventBus;
class IServiceLocator;
class FSVOHybridVolume;
struct FRegionID;
struct FBuffer;

// Interface for cross-region processing
class ICrossRegionProcessor
{
public:
    virtual ~ICrossRegionProcessor() {}
    
    virtual bool BeginCrossRegionOperation(TArray<FRegionID> RegionIDs, EBoundaryOperationType OperationType) = 0;
    // Add more pure virtual functions as needed
};

// Enums for cross-region processing
enum class EBoundaryOperationType : uint8
{
    None,
    MaterialBlending,
    FeatureContinuity,
    GeometryStitching,
    TerrainMatching,
    // Add more types as needed
};

// Implementation class
class FCrossRegionProcessor : public ICrossRegionProcessor
{
public:
    FCrossRegionProcessor();
    virtual ~FCrossRegionProcessor();
    
    // ICrossRegionProcessor interface
    virtual bool BeginCrossRegionOperation(TArray<FRegionID> RegionIDs, EBoundaryOperationType OperationType) override;
    
private:
    // Event handling
    void OnRegionBoundaryEvent(const FRegionBoundaryEvent& Event);
    
    // Service references
    TSharedPtr<IServiceLocator> ServiceLocator;
    TSharedPtr<IRegionManager> RegionManager;
    TSharedPtr<IMemoryManager> MemoryManager;
    TSharedPtr<ITaskScheduler> TaskScheduler;
    TSharedPtr<ITransactionManager> TransactionManager;
    TSharedPtr<UEventBus> EventBus;
    
    // Operational state
    FThreadSafeBool bIsProcessingBoundary;
    EBoundaryOperationType CurrentBoundaryOperation;
    TArray<FRegionID> CurrentRegionIDs;
    int32 CurrentRegionCount;
    FGuid CurrentTransactionID;
    
    // Region data
    TMap<FRegionID, FSVOHybridVolume*> InvolvedVolumes;
    FCriticalSection RegionMutex;
    
    // Shared buffer for cross-region operations
    FBuffer* BoundarySharedBuffer;
    static constexpr uint32 CrossRegionBufferSize = 1 * 1024 * 1024; // 1MB
};