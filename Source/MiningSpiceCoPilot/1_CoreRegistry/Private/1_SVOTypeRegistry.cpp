// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVOTypeRegistry.h"
#include "Logging/LogMacros.h"
#include "HAL/PlatformProcess.h"
#include "Math/UnrealMathUtility.h"
// Add memory management includes
#include "Interfaces/IMemoryManager.h"
#include "MemoryPoolManager.h"
#include "Interfaces/IServiceLocator.h"
#include "ThreadSafety.h"
#include "SVOAllocator.h" // Include SVOAllocator for ConfigureTypeLayout
#include "HAL/ThreadSafeCounter.h" // For atomic operations
#include "../../3_ThreadingTaskSystem/Public/TaskHelpers.h"
#include "../../3_ThreadingTaskSystem/Public/ParallelExecutor.h"
#include "Logging/LogMining.h"

// Initialize static members
FSVOTypeRegistry* FSVOTypeRegistry::Singleton = nullptr;
FThreadSafeBool FSVOTypeRegistry::bSingletonInitialized = false;

FSVOTypeRegistry::FSVOTypeRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , bIsInitialized(false)
    , SchemaVersion(1) // Initial schema version
    , bSIMDCapabilitiesDetected(false)
    , bSupportsSSE2(false)
    , bSupportsAVX(false)
    , bSupportsAVX2(false)
    , bSupportsAVX512(false)
    , PoolContentionCount(0)
    , OptimisticLockFailures(0)
{
    // Constructor is intentionally minimal
}

FSVOTypeRegistry::~FSVOTypeRegistry()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FSVOTypeRegistry::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return false;
    }
    
    // Set initialized flag
    bIsInitialized.AtomicSet(true);
    
    // Initialize internal maps
    NodeTypeMap.Empty();
    NodeTypeNameMap.Empty();
    
    // Reset type ID counter
    NextTypeId.Set(1);
    
    // Detect CPU capabilities for SIMD operations
    DetectSIMDCapabilities();
    
    return true;
}

void FSVOTypeRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopedSpinLock Lock(this->RegistryLock);
        
        // Clear all registered types
        NodeTypeMap.Empty();
        NodeTypeNameMap.Empty();
        
        // Reset state
        bIsInitialized = false;
    }
}

bool FSVOTypeRegistry::IsInitialized() const
{
    return bIsInitialized;
}

FName FSVOTypeRegistry::GetRegistryName() const
{
    return FName(TEXT("SVOTypeRegistry"));
}

uint32 FSVOTypeRegistry::GetSchemaVersion() const
{
    return SchemaVersion;
}

bool FSVOTypeRegistry::Validate(TArray<FString>& OutErrors) const
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("SVO Type Registry is not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    bool bIsValid = true;
    
    // Verify name-to-ID map integrity
    for (const auto& TypeNamePair : NodeTypeNameMap)
    {
        const FName& TypeName = TypeNamePair.Key;
        const uint32 TypeId = TypeNamePair.Value;
        
        if (!NodeTypeMap.Contains(TypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("SVO type name '%s' references non-existent type ID %u"), *TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        else if (NodeTypeMap[TypeId]->TypeName != TypeName)
        {
            OutErrors.Add(FString::Printf(TEXT("SVO type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *TypeName.ToString(), TypeId, *NodeTypeMap[TypeId]->TypeName.ToString()));
            bIsValid = false;
        }
    }
    
    // Verify type ID-to-info map integrity
    for (const auto& TypeInfoPair : NodeTypeMap)
    {
        const uint32 TypeId = TypeInfoPair.Key;
        const TSharedRef<FSVONodeTypeInfo>& TypeInfo = TypeInfoPair.Value;
        
        if (!NodeTypeNameMap.Contains(TypeInfo->TypeName))
        {
            OutErrors.Add(FString::Printf(TEXT("SVO type ID %u ('%s') not found in name map"),
                TypeId, *TypeInfo->TypeName.ToString()));
            bIsValid = false;
        }
        else if (NodeTypeNameMap[TypeInfo->TypeName] != TypeId)
        {
            OutErrors.Add(FString::Printf(TEXT("SVO type ID mismatch: ID %u maps to name '%s', but name maps to ID %u"),
                TypeId, *TypeInfo->TypeName.ToString(), NodeTypeNameMap[TypeInfo->TypeName]));
            bIsValid = false;
        }
        
        // Validate alignment requirement (must be power of 2)
        if ((TypeInfo->AlignmentRequirement & (TypeInfo->AlignmentRequirement - 1)) != 0)
        {
            OutErrors.Add(FString::Printf(TEXT("SVO type '%s' (ID %u) has invalid alignment requirement %u (must be power of 2)"),
                *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->AlignmentRequirement));
            bIsValid = false;
        }
        
        // Validate SIMD compatibility
        if (TypeInfo->bSupportsSIMD)
        {
            if (TypeInfo->AlignmentRequirement < 16)
            {
                OutErrors.Add(FString::Printf(TEXT("SVO type '%s' (ID %u) supports SIMD but has insufficient alignment %u (must be at least 16)"),
                    *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->AlignmentRequirement));
                bIsValid = false;
            }
            
            // Check that the SIMD instruction set is supported by the current hardware
            if (!IsSIMDInstructionSetSupported(TypeInfo->RequiredInstructionSet))
            {
                OutErrors.Add(FString::Printf(TEXT("SVO type '%s' (ID %u) requires SIMD instruction set %d which is not supported by current hardware"),
                    *TypeInfo->TypeName.ToString(), TypeId, static_cast<int32>(TypeInfo->RequiredInstructionSet)));
                // Not marking as invalid since this is a runtime capability issue, not a logical error
            }
        }
    }
    
    return bIsValid;
}

void FSVOTypeRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety
        FScopedSpinLock Lock(this->RegistryLock);
        
        // Clear all registered types
        NodeTypeMap.Empty();
        NodeTypeNameMap.Empty();
        
        // Reset type ID counter
        NextTypeId.Set(1);
    }
}

bool FSVOTypeRegistry::SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot set type version - registry not initialized"));
        return false;
    }
    
    // Check if type exists
    if (!NodeTypeMap.Contains(TypeId))
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot set type version - type ID %u not found"), TypeId);
        return false;
    }
    
    // Use scoped lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Get mutable type info
    TSharedRef<FSVONodeTypeInfo>& TypeInfo = NodeTypeMap[TypeId];
    
    // If version is the same, nothing to do
    if (TypeInfo->SchemaVersion == NewVersion)
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Type '%s' is already at version %u"),
            *TypeInfo->TypeName.ToString(), NewVersion);
        return true;
    }
    
    // Store old version for logging
    uint32 OldVersion = TypeInfo->SchemaVersion;
    
    // Update type version
    TypeInfo->SchemaVersion = NewVersion;
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Updated type '%s' version from %u to %u"),
        *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
    
    // If migration is not requested, we're done
    if (!bMigrateInstanceData)
    {
        return true;
    }
    
    // Integrate with MemoryPoolManager to update memory state
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Memory migration skipped for type '%s' - Memory Manager not available"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
    
    // Check if memory manager supports type migrations
    if (MemoryManager && MemoryManager->GetPoolForType(TypeId))
    {
        // Create memory migration data
        FTypeVersionMigrationInfo MigrationInfo;
        MigrationInfo.TypeId = TypeId;
        MigrationInfo.TypeName = TypeInfo->TypeName;
        MigrationInfo.OldVersion = OldVersion;
        MigrationInfo.NewVersion = NewVersion;
        MigrationInfo.DataSize = TypeInfo->DataSize;
        MigrationInfo.AlignmentRequirement = TypeInfo->AlignmentRequirement;
        
        // Perform migration through the pool
        bool bMigrationSuccess = MemoryManager->GetPoolForType(TypeId)->UpdateTypeVersion(MigrationInfo);
        
        if (bMigrationSuccess)
        {
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Successfully migrated memory for type '%s' from version %u to %u"),
                *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
        }
        else
        {
            UE_LOG(LogSVOTypeRegistry, Error, TEXT("Failed to migrate memory for type '%s' from version %u to %u"),
                *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
        }
        
        return bMigrationSuccess;
    }
    else
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Memory migration skipped for type '%s' - Pool not found"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
}

uint32 FSVOTypeRegistry::GetTypeVersion(uint32 TypeId) const
{
    // Check if type exists
    if (!NodeTypeMap.Contains(TypeId))
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("GetTypeVersion - type ID %u not found"), TypeId);
        return 0;
    }
    
    // Get type info
    const TSharedRef<FSVONodeTypeInfo>& TypeInfo = NodeTypeMap[TypeId];
    
    return TypeInfo->SchemaVersion;
}

uint32 FSVOTypeRegistry::RegisterNodeType(
    const FName& InTypeName,
    ESVONodeClass InNodeClass,
    uint32 InDataSize,
    uint32 InAlignmentRequirement,
    bool bInSupportsMaterialRelationships)
{
    // Check if registry is initialized
    if (!IsInitialized())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register node type - registry not initialized"));
        return 0;
    }
    
    // Validate parameters
    if (InTypeName.IsNone())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register node type - name is None"));
        return 0;
    }
    
    if (InDataSize == 0)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register node type '%s' - data size is 0"), *InTypeName.ToString());
        return 0;
    }
    
    // Alignment must be a power of 2
    if ((InAlignmentRequirement & (InAlignmentRequirement - 1)) != 0)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register node type '%s' - alignment %u is not a power of 2"),
            *InTypeName.ToString(), InAlignmentRequirement);
        return 0;
    }
    
    // Try optimistic path first - check without locking if type already exists
    if (IsNodeTypeRegistered(InTypeName))
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Node type '%s' is already registered"), *InTypeName.ToString());
        const FSVONodeTypeInfo* ExistingInfo = GetNodeTypeInfoByName(InTypeName);
        return ExistingInfo ? ExistingInfo->TypeId : 0;
    }

    bool bIsContentionHigh = false;
    
    // Track lock contention for diagnostics
    if (PoolContentionCount.Increment() > 10)
    {
        // High contention detected, log this for performance analysis
        UE_LOG(LogSVOTypeRegistry, Verbose, TEXT("High contention detected for node type registration"));
        bIsContentionHigh = true;
    }
    
    // Use read-write scoped lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Double-check that the type isn't already registered (needed for thread safety)
    if (NodeTypeNameMap.Contains(InTypeName))
    {
        PoolContentionCount.Decrement();
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Node type '%s' is already registered (after lock)"), *InTypeName.ToString());
        return NodeTypeNameMap[InTypeName];
    }
    
    // Generate a unique ID for this type
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create node type info
    TSharedRef<FSVONodeTypeInfo> NodeTypeInfo = MakeShared<FSVONodeTypeInfo>();
    NodeTypeInfo->TypeId = TypeId;
    NodeTypeInfo->TypeName = InTypeName;
    NodeTypeInfo->NodeClass = InNodeClass;
    NodeTypeInfo->SchemaVersion = 1; // Initial version
    NodeTypeInfo->AlignmentRequirement = InAlignmentRequirement;
    NodeTypeInfo->DataSize = InDataSize;
    NodeTypeInfo->bSupportsMaterialRelationships = bInSupportsMaterialRelationships;
    
    // Automatically detect SIMD capabilities
    if ((InAlignmentRequirement % 16) == 0 && IsSIMDInstructionSetSupported(ESIMD_InstructionSet::SSE2))
    {
        NodeTypeInfo->bSupportsSIMD = true;
        NodeTypeInfo->RequiredInstructionSet = ESIMD_InstructionSet::SSE2;
        NodeTypeInfo->AddCapability(ESVONodeCapabilities::SupportsSIMD);
    }
    
    // Set default memory layout
    NodeTypeInfo->MemoryLayout = ESVOMemoryLayout::Sequential;
    
    // Add the type info to maps
    NodeTypeMap.Add(TypeId, NodeTypeInfo);
    NodeTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Registered node type '%s' with ID %u, size %u, alignment %u"),
        *InTypeName.ToString(), TypeId, InDataSize, InAlignmentRequirement);
    
    // Create a specialized memory pool for this type
    CreateTypeSpecificPool(NodeTypeInfo);
    
    // Configure memory pool capabilities based on type characteristics
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (MemoryManager)
    {
        // Set appropriate access pattern based on node type
        EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::General;
        switch (InNodeClass)
        {
            case ESVONodeClass::Homogeneous:
                AccessPattern = EMemoryAccessPattern::Sequential;
                break;
            case ESVONodeClass::Interface:
                AccessPattern = EMemoryAccessPattern::Random;
                break;
            case ESVONodeClass::Empty:
                AccessPattern = EMemoryAccessPattern::General;
                break;
            case ESVONodeClass::Custom:
                AccessPattern = EMemoryAccessPattern::General;
                break;
        }
        
        // Configure memory layout based on type
        uint32 MemoryLayout = static_cast<uint32>(NodeTypeInfo->MemoryLayout);
        
        // Configure pool capabilities in memory manager
        FMemoryPoolManager* PoolManager = static_cast<FMemoryPoolManager*>(MemoryManager);
        if (PoolManager)
        {
            ConfigurePoolCapabilities(PoolManager, NodeTypeInfo);
        }
    }
    
    // Decrement contention counter
    PoolContentionCount.Decrement();
    
    return TypeId;
}

/**
 * Creates a type-specific memory pool for a node type
 * Integrates with MemoryPoolManager to optimize memory allocation for each node type
 * @param TypeInfo Node type information
 */
void FSVOTypeRegistry::CreateTypeSpecificPool(const TSharedRef<FSVONodeTypeInfo>& TypeInfo)
{
    // Get memory manager from service locator
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Could not create memory pool for type '%s' - Memory Manager not available"),
            *TypeInfo->TypeName.ToString());
        return;
    }
    
    // Estimate an appropriate initial pool size based on node type
    uint32 EstimatedNodeCount = 1024; // Default
    switch (TypeInfo->NodeClass)
    {
        case ESVONodeClass::Homogeneous:
            EstimatedNodeCount = 4096; // Homogeneous nodes are more common
            break;
        case ESVONodeClass::Interface:
            EstimatedNodeCount = 2048; // Interface nodes appear at material boundaries
            break;
        case ESVONodeClass::Empty:
            EstimatedNodeCount = 512; // Empty nodes are less common in most scenes
            break;
        case ESVONodeClass::Custom:
            EstimatedNodeCount = 1024; // Default for custom nodes
            break;
    }
    
    // Create a pool name using type ID for uniqueness
    FName PoolName = *FString::Printf(TEXT("SVONodePool_%s_%u"), *TypeInfo->TypeName.ToString(), TypeInfo->TypeId);
    
    // Integrate with memory pool manager to create a type-specific pool
    FMemoryPoolManager* PoolManager = static_cast<FMemoryPoolManager*>(MemoryManager);
    if (PoolManager)
    {
        // Create a specialized SVO node pool
        PoolManager->CreateSVONodePool(
            PoolName,
            TypeInfo->DataSize,
            EstimatedNodeCount
        );
    }
}

void FSVOTypeRegistry::ConfigurePoolCapabilities(FMemoryPoolManager* PoolManager, const TSharedRef<FSVONodeTypeInfo>& TypeInfo)
{
    if (!PoolManager)
    {
        return;
    }
    
    // Configure memory layout optimization based on node capabilities
    
    // Check if the type supports SIMD operations
    if (TypeInfo->bSupportsSIMD)
    {
        // Check if the required instruction set is supported by the hardware
        if (IsSIMDInstructionSetSupported(TypeInfo->RequiredInstructionSet))
        {
            // Apply SIMD-specific memory optimizations
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Configuring SIMD-optimized memory layout for node type '%s'"), 
                *TypeInfo->TypeName.ToString());
            
            // Here we would configure SIMD-specific memory layouts
            // For now, we just log that it would happen
        }
        else
        {
            // SIMD not supported, set up fallback path
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Hardware doesn't support required SIMD instruction set for node type '%s', using fallback path"), 
                *TypeInfo->TypeName.ToString());
            
            // Configure fallback memory layout
        }
    }
    
    // Check if the type supports concurrent access
    if (TypeInfo->bSupportsConcurrentAccess)
    {
        // Apply thread-safe memory optimizations
        UE_LOG(LogSVOTypeRegistry, Log, TEXT("Configuring thread-safe memory layout for node type '%s'"), 
            *TypeInfo->TypeName.ToString());
        
        // Configure memory barriers and thread-safe access patterns
    }
    
    // Apply memory layout optimizations based on the node's memory layout preference
    switch (TypeInfo->MemoryLayout)
    {
        case ESVOMemoryLayout::Sequential:
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Configuring sequential memory layout for node type '%s'"), 
                *TypeInfo->TypeName.ToString());
            // Configure sequential memory access optimizations
            break;
            
        case ESVOMemoryLayout::Interleaved:
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Configuring interleaved memory layout for node type '%s'"), 
                *TypeInfo->TypeName.ToString());
            // Configure interleaved memory access optimizations
            break;
            
        case ESVOMemoryLayout::Tiled:
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Configuring tiled memory layout for node type '%s'"), 
                *TypeInfo->TypeName.ToString());
            // Configure tiled memory access optimizations
            break;
            
        default:
            UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Unknown memory layout for node type '%s', using default layout"), 
                *TypeInfo->TypeName.ToString());
            // Use default memory layout
            break;
    }
    
    // Configure platform-specific memory optimizations
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Applying platform-specific memory optimizations for node type '%s'"), 
        *TypeInfo->TypeName.ToString());
}

const FSVONodeTypeInfo* FSVOTypeRegistry::GetNodeTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Look up type by ID
    const TSharedRef<FSVONodeTypeInfo>* TypeInfoPtr = NodeTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        return &(TypeInfoPtr->Get());
    }
    
    return nullptr;
}

const FSVONodeTypeInfo* FSVOTypeRegistry::GetNodeTypeInfoByName(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = NodeTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        const TSharedRef<FSVONodeTypeInfo>* TypeInfoPtr = NodeTypeMap.Find(*TypeIdPtr);
        if (TypeInfoPtr)
        {
            return &(TypeInfoPtr->Get());
        }
    }
    
    return nullptr;
}

TArray<FSVONodeTypeInfo> FSVOTypeRegistry::GetAllNodeTypes() const
{
    TArray<FSVONodeTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Collect all type infos
    Result.Reserve(NodeTypeMap.Num());
    for (const auto& TypeInfoPair : NodeTypeMap)
    {
        Result.Add(TypeInfoPair.Value.Get());
    }
    
    return Result;
}

TArray<FSVONodeTypeInfo> FSVOTypeRegistry::GetNodeTypesByClass(ESVONodeClass InNodeClass) const
{
    TArray<FSVONodeTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Collect type infos matching the class
    for (const auto& TypeInfoPair : NodeTypeMap)
    {
        const TSharedRef<FSVONodeTypeInfo>& TypeInfo = TypeInfoPair.Value;
        if (TypeInfo->NodeClass == InNodeClass)
        {
            Result.Add(TypeInfo.Get());
        }
    }
    
    return Result;
}

bool FSVOTypeRegistry::IsNodeTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    return NodeTypeMap.Contains(InTypeId);
}

bool FSVOTypeRegistry::IsNodeTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    return NodeTypeNameMap.Contains(InTypeName);
}

FSVOTypeRegistry& FSVOTypeRegistry::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        if (!bSingletonInitialized.AtomicSet(true))
        {
            Singleton = new FSVOTypeRegistry();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

uint32 FSVOTypeRegistry::GenerateUniqueTypeId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextTypeId.Increment();
}

/**
 * Detects available SIMD instruction sets on the current CPU
 */
void FSVOTypeRegistry::DetectSIMDCapabilities()
{
    // Set default values based on compile-time platform capabilities
#if PLATFORM_ENABLE_VECTORINTRINSICS
    bSupportsSSE2 = true; // SSE2 is guaranteed on x86-64 platforms that support vector intrinsics
#else
    bSupportsSSE2 = false;
#endif

    // Check for AVX capabilities - in UE5 we need to use platform-specific approaches
    // These are conservative assumptions that work across platforms
    bSupportsAVX = false;
    bSupportsAVX2 = false;
    bSupportsAVX512 = false;

    UE_LOG(LogTemp, Verbose, TEXT("FSVOTypeRegistry::DetectSIMDCapabilities - SSE2: %d, AVX: %d, AVX2: %d, AVX512: %d"),
        bSupportsSSE2, bSupportsAVX, bSupportsAVX2, bSupportsAVX512);
}

/**
 * Checks if the specified SIMD instruction set is supported by the current hardware
 */
bool FSVOTypeRegistry::IsSIMDInstructionSetSupported(ESIMD_InstructionSet InInstructionSet) const
{
    switch (InInstructionSet)
    {
        case ESIMD_InstructionSet::None:
            return true;
            
        case ESIMD_InstructionSet::SSE2:
            return bSupportsSSE2;
            
        case ESIMD_InstructionSet::AVX:
            return bSupportsAVX;
            
        case ESIMD_InstructionSet::AVX2:
            return bSupportsAVX2;
            
        case ESIMD_InstructionSet::AVX512:
            return bSupportsAVX512;
            
        default:
            return false;
    }
}

bool FSVOTypeRegistry::RegisterCapabilities(uint32 TypeId, uint32 Capabilities)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register capabilities - registry not initialized"));
        return false;
    }
    
    // Check if type exists
    if (!NodeTypeMap.Contains(TypeId))
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot register capabilities - type ID %u not found"), TypeId);
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Get mutable type info
    TSharedRef<FSVONodeTypeInfo>& TypeInfo = NodeTypeMap[TypeId];
    
    // Update capabilities
    TypeInfo->CapabilitiesFlags |= Capabilities;
    
    // Check if we need to update memory manager's pool settings
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        return false;
    }
    
    // Check if memory manager supports capability updates
    if (MemoryManager && MemoryManager->GetPoolForType(TypeId))
    {
        // Notify memory manager of capability changes
        // Implementation depends on your memory manager API
    }
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Updated capabilities for type '%s' (ID %u)"),
        *TypeInfo->TypeName.ToString(), TypeId);
    
    return true;
}

bool FSVOTypeRegistry::OptimizeNodeLayout(uint32 TypeId, bool bUseZOrderCurve, bool bEnablePrefetching)
{
    if (!IsInitialized())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot optimize node layout - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Check if type exists
    if (!NodeTypeMap.Contains(TypeId))
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot optimize node layout - type ID %u not found"), TypeId);
        return false;
    }
    
    // Get node type info
    const TSharedRef<FSVONodeTypeInfo>& TypeInfo = NodeTypeMap[TypeId];
    
    // Get memory management service from service locator
    IServiceLocator* ServiceLocator = &IServiceLocator::Get();
    if (!ServiceLocator)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot optimize node layout - ServiceLocator not available"));
        return false;
    }
    
    // Get SVO allocator
    // Using default zone and region IDs (INDEX_NONE) for service resolution
    FSVOAllocator* SVOAllocator = ServiceLocator->ResolveService<FSVOAllocator>(INDEX_NONE, INDEX_NONE);
    if (!SVOAllocator)
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot optimize node layout - SVOAllocator not available"));
        return false;
    }
    
    // Determine appropriate memory access pattern based on node type
    EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::OctreeTraversal;
    switch (TypeInfo->NodeClass)
    {
        case ESVONodeClass::Homogeneous:
            AccessPattern = EMemoryAccessPattern::Sequential;
            break;
        case ESVONodeClass::Interface:
            AccessPattern = EMemoryAccessPattern::Random;
            break;
        case ESVONodeClass::Empty:
            // Use a different access pattern for empty nodes
            AccessPattern = EMemoryAccessPattern::General;
            break;
        case ESVONodeClass::Custom:
            // Use general access pattern for custom nodes
            AccessPattern = EMemoryAccessPattern::General;
            break;
    }
    
    // Override Z-order curve usage for certain node types
    if (TypeInfo->NodeClass == ESVONodeClass::Empty)
    {
        // Empty nodes don't benefit much from Z-order curve
        bUseZOrderCurve = false;
    }
    
    // Configure memory layout in the SVO allocator
    bool bResult = SVOAllocator->ConfigureTypeLayout(
        TypeId,
        bUseZOrderCurve,
        bEnablePrefetching,
        AccessPattern
    );
    
    if (bResult)
    {
        UE_LOG(LogSVOTypeRegistry, Log, TEXT("Optimized node layout for type '%s' (ID %u) with Z-order curve %s, prefetching %s"),
            *TypeInfo->TypeName.ToString(), TypeId, 
            bUseZOrderCurve ? TEXT("enabled") : TEXT("disabled"),
            bEnablePrefetching ? TEXT("enabled") : TEXT("disabled"));
    }
    else
    {
        UE_LOG(LogSVOTypeRegistry, Warning, TEXT("Failed to optimize node layout for type '%s' (ID %u)"),
            *TypeInfo->TypeName.ToString(), TypeId);
    }
    
    return bResult;
}

// Add to support optimistic locking for type registration
bool FSVOTypeRegistry::TryOptimisticRegisterNodeType(
    const FName& InTypeName, 
    ESVONodeClass InNodeClass, 
    uint32 InDataSize, 
    uint32 InAlignmentRequirement, 
    bool bInSupportsMaterialRelationships)
{
    // This method provides a faster registration path with optimistic locking
    // It attempts to register without acquiring the registry lock first
    
    // Quick check if already initialized and type not already registered
    if (!IsInitialized() || IsNodeTypeRegistered(InTypeName))
    {
        return false;
    }
    
    // Try to perform registration without locking
    // If we detect conditions have changed, we'll fallback to the regular path
    
    // Generate a tentative ID (we'll confirm it's unique later)
    uint32 TentativeTypeId = NextTypeId.GetValue();
    
    // Optimistically create node type info
    TSharedRef<FSVONodeTypeInfo> NodeTypeInfo = MakeShared<FSVONodeTypeInfo>();
    NodeTypeInfo->TypeId = TentativeTypeId;
    NodeTypeInfo->TypeName = InTypeName;
    NodeTypeInfo->NodeClass = InNodeClass;
    NodeTypeInfo->SchemaVersion = 1;
    NodeTypeInfo->AlignmentRequirement = InAlignmentRequirement;
    NodeTypeInfo->DataSize = InDataSize;
    NodeTypeInfo->bSupportsMaterialRelationships = bInSupportsMaterialRelationships;
    
    // Now try to acquire the lock and update if nothing has changed
    bool bLockAcquired = false;
    {
        FScopedSpinLock Lock(this->RegistryLock);
        
        // Check if conditions are still valid
        if (!NodeTypeNameMap.Contains(InTypeName) && NextTypeId.GetValue() == TentativeTypeId)
        {
            // Update the next type ID
            NextTypeId.Increment();
            
            // Add the type info to maps
            NodeTypeMap.Add(TentativeTypeId, NodeTypeInfo);
            NodeTypeNameMap.Add(InTypeName, TentativeTypeId);
            
            // Create memory pool (while still holding lock)
            CreateTypeSpecificPool(NodeTypeInfo);
            
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Optimistically registered node type '%s' with ID %u"),
                *InTypeName.ToString(), TentativeTypeId);
            
            return true;
        }
        
        // Conditions changed, fall back to standard path
        OptimisticLockFailures.Increment();
        
        // Log that optimistic approach failed
        UE_LOG(LogSVOTypeRegistry, Verbose, TEXT("Optimistic registration failed for '%s', falling back"),
            *InTypeName.ToString());
    }
    
    return false;
}

void FSVOTypeRegistry::SynchronizePoolCreation(uint32 TypeId)
{
    // This method helps synchronize memory pool creation across threads
    // to avoid redundant pools and contention
    if (!IsNodeTypeRegistered(TypeId))
    {
        return;
    }
    
    // Get memory manager from service locator
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        return;
    }
    
    // Check if pool already exists for this type
    if (MemoryManager->GetPoolForType(TypeId))
    {
        // Pool already exists, nothing to do
        return;
    }
    
    // Use a short scope lock to avoid contention
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Double check if pool exists after acquiring lock
    if (MemoryManager->GetPoolForType(TypeId))
    {
        return;
    }
    
    // Pool doesn't exist yet, create it
    const FSVONodeTypeInfo* TypeInfo = GetNodeTypeInfo(TypeId);
    if (TypeInfo)
    {
        // Create a type-specific pool if needed
        FMemoryPoolManager* PoolManager = static_cast<FMemoryPoolManager*>(MemoryManager);
        if (PoolManager)
        {
            // Create a pool name using type ID for uniqueness
            FName PoolName = *FString::Printf(TEXT("SVONodePool_%s_%u"), *TypeInfo->TypeName.ToString(), TypeInfo->TypeId);
            
            // Determine appropriate node count
            uint32 EstimatedNodeCount = 1024;
            switch (TypeInfo->NodeClass)
            {
                case ESVONodeClass::Homogeneous: EstimatedNodeCount = 4096; break;
                case ESVONodeClass::Interface: EstimatedNodeCount = 2048; break;
                case ESVONodeClass::Empty: EstimatedNodeCount = 512; break;
                case ESVONodeClass::Custom: EstimatedNodeCount = 1024; break;
            }
            
            // Create the pool
            PoolManager->CreateSVONodePool(
                PoolName,
                TypeInfo->DataSize,
                EstimatedNodeCount
            );
        }
    }
}

ERegistryType FSVOTypeRegistry::GetRegistryType() const
{
    return ERegistryType::SVO;
}

ETypeCapabilities FSVOTypeRegistry::GetTypeCapabilities(uint32 TypeId) const
{
    // Start with no capabilities
    ETypeCapabilities Capabilities = ETypeCapabilities::None;
    
    // Check if this type is registered
    if (!IsNodeTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the type info
    const FSVONodeTypeInfo* TypeInfo = GetNodeTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map SVO capabilities to type capabilities
    if (TypeInfo->bSupportsThreading)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::ThreadSafe);
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::ParallelProcessing);
    }
    
    if (TypeInfo->bSupportsSIMD)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::SIMDOperations);
    }
    
    if (TypeInfo->bSupportsIncrementalUpdates)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::IncrementalUpdates);
    }
    
    // Check for batch operation support
    if (TypeInfo->PreferredThreadBlockSize > 1)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::BatchOperations);
    }
    
    // Check for async operation support
    if (TypeInfo->bSupportsThreading && TypeInfo->bSupportsConcurrentAccess)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::AsyncOperations);
    }
    
    // Check for partial execution support
    if (TypeInfo->bSupportsIncrementalUpdates && TypeInfo->bSupportsThreading)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::PartialExecution);
    }
    
    // Check for result merging support
    if (TypeInfo->bSupportsThreading && 
        (TypeInfo->NodeClass == ESVONodeClass::Homogeneous || 
         TypeInfo->NodeClass == ESVONodeClass::Interface))
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::ResultMerging);
    }
    
    return Capabilities;
}

uint64 FSVOTypeRegistry::ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config)
{
    // Create a type-specific task configuration
    FTaskConfig TypedConfig = Config;
    TypedConfig.SetTypeId(TypeId, ERegistryType::SVO);
    
    // Set optimization flags based on type capabilities
    ETypeCapabilities BasicCapabilities = GetTypeCapabilities(TypeId);
    ETypeCapabilitiesEx ExtendedCapabilities = GetTypeCapabilitiesEx(TypeId);
    EThreadOptimizationFlags OptimizationFlags = FTaskScheduler::MapCapabilitiesToOptimizationFlags(
        BasicCapabilities, ExtendedCapabilities);
    
    TypedConfig.SetOptimizationFlags(OptimizationFlags);
    
    // Schedule the task with the scheduler
    return ScheduleTaskWithScheduler(TaskFunc, TypedConfig);
}

bool FSVOTypeRegistry::RegisterNodeTypesBatch(
    const TArray<FSVONodeTypeInfo>& TypeInfos,
    TArray<uint32>& OutTypeIds,
    TArray<FString>& OutErrors,
    FParallelConfig Config)
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Cannot register node types - registry not initialized"));
        return false;
    }
    
    if (TypeInfos.Num() == 0)
    {
        // Nothing to register, consider it successful
        return true;
    }
    
    // Clear output arrays
    OutTypeIds.Reset(TypeInfos.Num());
    OutTypeIds.SetNum(TypeInfos.Num());
    
    // First prevalidate all types
    if (!PrevalidateNodeTypes(TypeInfos, OutErrors, true))
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Batch node type registration failed: validation errors"));
        return false;
    }
    
    // For very small batches (<=3), just use sequential processing
    if (TypeInfos.Num() <= 3 || Config.ExecutionMode == EParallelExecutionMode::ForceSequential)
    {
        FScopedSpinLock Lock(this->RegistryLock);
        
        for (int32 Index = 0; Index < TypeInfos.Num(); ++Index)
        {
            const FSVONodeTypeInfo& TypeInfo = TypeInfos[Index];
            
            // Check for duplicate names
            if (NodeTypeNameMap.Contains(TypeInfo.TypeName))
            {
                OutErrors.Add(FString::Printf(TEXT("Type with name '%s' already registered"), *TypeInfo.TypeName.ToString()));
                OutTypeIds[Index] = 0; // Set ID to 0 (invalid) for failed registrations
                continue;
            }
            
            // Generate a unique type ID
            uint32 TypeId = GenerateUniqueTypeId();
            
            // Create a copy of the type info to store
            TSharedRef<FSVONodeTypeInfo> NewTypeInfo = MakeShared<FSVONodeTypeInfo>(TypeInfo);
            NewTypeInfo->TypeId = TypeId;
            
            // Register the new type
            NodeTypeMap.Add(TypeId, NewTypeInfo);
            NodeTypeNameMap.Add(NewTypeInfo->TypeName, TypeId);
            
            // Store the assigned type ID
            OutTypeIds[Index] = TypeId;
            
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Registered SVO node type '%s' (ID %u)"),
                *NewTypeInfo->TypeName.ToString(), TypeId);
            
            // Create memory pool for this type
            CreateTypeSpecificPool(NewTypeInfo);
        }
        
        return true;
    }
    
    // For larger batches, use parallel processing with optimistic registration
    // Use atomic operations and lock-free approaches where possible
    
    // First, reserve type IDs for all types to avoid ID collisions during parallel processing
    TArray<uint32> ReservedTypeIds;
    {
        FScopedSpinLock Lock(this->RegistryLock);
        ReservedTypeIds.SetNum(TypeInfos.Num());
        
        for (int32 Index = 0; Index < TypeInfos.Num(); ++Index)
        {
            ReservedTypeIds[Index] = GenerateUniqueTypeId();
        }
        
        // Quick precheck for duplicate names to fail fast
        TSet<FName> TypeNames;
        for (const FSVONodeTypeInfo& TypeInfo : TypeInfos)
        {
            if (NodeTypeNameMap.Contains(TypeInfo.TypeName) || TypeNames.Contains(TypeInfo.TypeName))
            {
                OutErrors.Add(FString::Printf(TEXT("Type with name '%s' already registered"), *TypeInfo.TypeName.ToString()));
                return false;
            }
            TypeNames.Add(TypeInfo.TypeName);
        }
    }
    
    // Setup shared struct for parallel processing
    struct FRegistrationContext
    {
        FCriticalSection ContextLock;
        TArray<uint32> SuccessIndices;
        TArray<TSharedRef<FSVONodeTypeInfo>> RegisteredTypes;
        TArray<FString> LocalErrors;
    };
    
    FRegistrationContext Context;
    Context.RegisteredTypes.Reserve(TypeInfos.Num());
    
    // Perform parallel registration
    bool bSuccess = FParallelExecutor::Get().ParallelFor(
        TypeInfos.Num(),
        [this, &TypeInfos, &ReservedTypeIds, &Context](int32 Index)
        {
            const FSVONodeTypeInfo& TypeInfo = TypeInfos[Index];
            uint32 TypeId = ReservedTypeIds[Index];
            
            // Create a copy of the type info to store
            TSharedRef<FSVONodeTypeInfo> NewTypeInfo = MakeShared<FSVONodeTypeInfo>(TypeInfo);
            NewTypeInfo->TypeId = TypeId;
            
            // Add to local collection
            {
                FScopeLock ContextLock(&Context.ContextLock);
                Context.RegisteredTypes.Add(NewTypeInfo);
                Context.SuccessIndices.Add(Index);
            }
            
            // Create memory pool without holding global lock
            CreateTypeSpecificPool(NewTypeInfo);
        },
        Config);
    
    // Now update the registry with the batch of new types
    if (Context.RegisteredTypes.Num() > 0)
    {
        FScopedSpinLock Lock(this->RegistryLock);
        
        // Add all successfully registered types to the registry
        for (int32 LocalIndex = 0; LocalIndex < Context.RegisteredTypes.Num(); ++LocalIndex)
        {
            const TSharedRef<FSVONodeTypeInfo>& NewTypeInfo = Context.RegisteredTypes[LocalIndex];
            int32 OriginalIndex = Context.SuccessIndices[LocalIndex];
            
            // Double-check for name collisions before adding
            if (NodeTypeNameMap.Contains(NewTypeInfo->TypeName))
            {
                Context.LocalErrors.Add(FString::Printf(TEXT("Type with name '%s' already registered during parallel processing"), 
                    *NewTypeInfo->TypeName.ToString()));
                OutTypeIds[OriginalIndex] = 0; // Invalid ID
                continue;
            }
            
            // Add to registries
            NodeTypeMap.Add(NewTypeInfo->TypeId, NewTypeInfo);
            NodeTypeNameMap.Add(NewTypeInfo->TypeName, NewTypeInfo->TypeId);
            
            // Store the assigned type ID
            OutTypeIds[OriginalIndex] = NewTypeInfo->TypeId;
            
            UE_LOG(LogSVOTypeRegistry, Log, TEXT("Registered SVO node type '%s' (ID %u) in batch"),
                *NewTypeInfo->TypeName.ToString(), NewTypeInfo->TypeId);
        }
    }
    
    // Add any errors from parallel processing
    OutErrors.Append(Context.LocalErrors);
    
    // Check for overall success - all types must be registered
    bool bAllRegistered = true;
    for (uint32 TypeId : OutTypeIds)
    {
        if (TypeId == 0)
        {
            bAllRegistered = false;
            break;
        }
    }
    
    return bAllRegistered;
}

bool FSVOTypeRegistry::PrevalidateNodeTypes(
    const TArray<FSVONodeTypeInfo>& TypeInfos,
    TArray<FString>& OutErrors,
    bool bParallel)
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Cannot validate node types - registry not initialized"));
        return false;
    }
    
    if (TypeInfos.Num() == 0)
    {
        // Nothing to validate, consider it successful
        return true;
    }
    
    // For very small batches or when parallel is disabled, use sequential validation
    if (!bParallel || TypeInfos.Num() <= 3)
    {
        bool bAllValid = true;
        
        for (int32 Index = 0; Index < TypeInfos.Num(); ++Index)
        {
            const FSVONodeTypeInfo& TypeInfo = TypeInfos[Index];
            
            // Check for duplicate type names
            if (IsNodeTypeRegistered(TypeInfo.TypeName))
            {
                OutErrors.Add(FString::Printf(TEXT("Type with name '%s' already registered"), *TypeInfo.TypeName.ToString()));
                bAllValid = false;
            }
            
            // Validate alignment requirement (must be power of 2)
            if ((TypeInfo.AlignmentRequirement & (TypeInfo.AlignmentRequirement - 1)) != 0)
            {
                OutErrors.Add(FString::Printf(TEXT("Type '%s' has invalid alignment requirement %u (must be power of 2)"),
                    *TypeInfo.TypeName.ToString(), TypeInfo.AlignmentRequirement));
                bAllValid = false;
            }
            
            // Validate data size (must be greater than 0)
            if (TypeInfo.DataSize == 0)
            {
                OutErrors.Add(FString::Printf(TEXT("Type '%s' has invalid data size 0 (must be greater than 0)"),
                    *TypeInfo.TypeName.ToString()));
                bAllValid = false;
            }
            
            // Validate SIMD compatibility
            if (TypeInfo.bSupportsSIMD && !IsSIMDInstructionSetSupported(TypeInfo.RequiredInstructionSet))
            {
                OutErrors.Add(FString::Printf(TEXT("Type '%s' requires SIMD instruction set '%d' which is not supported by the current hardware"),
                    *TypeInfo.TypeName.ToString(), static_cast<int32>(TypeInfo.RequiredInstructionSet)));
                bAllValid = false;
            }
        }
        
        return bAllValid;
    }
    
    // For larger batches, use parallel validation
    FTypeValidationContext ValidationContext;
    
    // First validate for duplicate names within the batch
    {
        TSet<FName> TypeNamesInBatch;
        for (const FSVONodeTypeInfo& TypeInfo : TypeInfos)
        {
            if (TypeNamesInBatch.Contains(TypeInfo.TypeName))
            {
                ValidationContext.AddError(FString::Printf(TEXT("Duplicate type name '%s' within batch"), 
                    *TypeInfo.TypeName.ToString()));
            }
            TypeNamesInBatch.Add(TypeInfo.TypeName);
        }
        
        // If we already have validation errors, no need to continue
        if (ValidationContext.Errors.Num() > 0)
        {
            OutErrors.Append(ValidationContext.Errors);
            return false;
        }
    }
    
    // Check if any type names are already registered (requires lock)
    {
        FScopedSpinLock Lock(this->RegistryLock);
        for (const FSVONodeTypeInfo& TypeInfo : TypeInfos)
        {
            if (NodeTypeNameMap.Contains(TypeInfo.TypeName))
            {
                ValidationContext.AddError(FString::Printf(TEXT("Type with name '%s' already registered"), 
                    *TypeInfo.TypeName.ToString()));
            }
        }
    }
    
    // Perform parallel validation for other criteria
    FParallelConfig Config;
    Config.SetExecutionMode(EParallelExecutionMode::ForceParallel);
    
    FParallelExecutor::Get().ParallelFor(
        TypeInfos.Num(),
        [this, &TypeInfos, &ValidationContext](int32 Index)
        {
            const FSVONodeTypeInfo& TypeInfo = TypeInfos[Index];
            
            // Validate alignment requirement (must be power of 2)
            if ((TypeInfo.AlignmentRequirement & (TypeInfo.AlignmentRequirement - 1)) != 0)
            {
                ValidationContext.AddError(FString::Printf(TEXT("Type '%s' has invalid alignment requirement %u (must be power of 2)"),
                    *TypeInfo.TypeName.ToString(), TypeInfo.AlignmentRequirement));
            }
            
            // Validate data size (must be greater than 0)
            if (TypeInfo.DataSize == 0)
            {
                ValidationContext.AddError(FString::Printf(TEXT("Type '%s' has invalid data size 0 (must be greater than 0)"),
                    *TypeInfo.TypeName.ToString()));
            }
            
            // Validate SIMD compatibility
            if (TypeInfo.bSupportsSIMD && !IsSIMDInstructionSetSupported(TypeInfo.RequiredInstructionSet))
            {
                ValidationContext.AddError(FString::Printf(TEXT("Type '%s' requires SIMD instruction set '%d' which is not supported by the current hardware"),
                    *TypeInfo.TypeName.ToString(), static_cast<int32>(TypeInfo.RequiredInstructionSet)));
            }
        },
        Config);
    
    // Check for validation errors
    if (ValidationContext.Errors.Num() > 0)
    {
        OutErrors.Append(ValidationContext.Errors);
        return false;
    }
    
    return true;
}

bool FSVOTypeRegistry::ValidateTypeConsistency(
    TArray<FString>& OutErrors,
    bool bParallel)
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Cannot validate type consistency - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FSVONodeTypeInfo> AllTypes = GetAllNodeTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to validate, consider it successful
        return true;
    }
    
    // For very small type counts or when parallel is disabled, use sequential validation
    if (!bParallel || AllTypes.Num() <= 3)
    {
        return Validate(OutErrors);
    }
    
    // For larger type counts, use parallel validation
    FTypeValidationContext ValidationContext;
    
    // Verify name-to-ID map integrity (requires lock)
    {
        FScopedSpinLock Lock(this->RegistryLock);
        
        for (const auto& TypeNamePair : NodeTypeNameMap)
        {
            const FName& TypeName = TypeNamePair.Key;
            const uint32 TypeId = TypeNamePair.Value;
            
            if (!NodeTypeMap.Contains(TypeId))
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type name '%s' references non-existent type ID %u"), 
                    *TypeName.ToString(), TypeId));
            }
            else if (NodeTypeMap[TypeId]->TypeName != TypeName)
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                    *TypeName.ToString(), TypeId, *NodeTypeMap[TypeId]->TypeName.ToString()));
            }
        }
    }
    
    // Perform parallel validation for individual type consistency
    FParallelConfig Config;
    Config.SetExecutionMode(EParallelExecutionMode::ForceParallel);
    
    FParallelExecutor::Get().ParallelFor(
        AllTypes.Num(),
        [this, &AllTypes, &ValidationContext](int32 Index)
        {
            const FSVONodeTypeInfo& TypeInfo = AllTypes[Index];
            const uint32 TypeId = TypeInfo.TypeId;
            
            // Validate type ID-to-info map integrity
            FScopedSpinLock Lock(this->RegistryLock);
            
            if (!NodeTypeNameMap.Contains(TypeInfo.TypeName))
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type ID %u ('%s') not found in name map"),
                    TypeId, *TypeInfo.TypeName.ToString()));
            }
            else if (NodeTypeNameMap[TypeInfo.TypeName] != TypeId)
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type ID mismatch: ID %u maps to name '%s', but name maps to ID %u"),
                    TypeId, *TypeInfo.TypeName.ToString(), NodeTypeNameMap[TypeInfo.TypeName]));
            }
            
            // Validate alignment requirement (must be power of 2)
            if ((TypeInfo.AlignmentRequirement & (TypeInfo.AlignmentRequirement - 1)) != 0)
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type '%s' (ID %u) has invalid alignment requirement %u (must be power of 2)"),
                    *TypeInfo.TypeName.ToString(), TypeId, TypeInfo.AlignmentRequirement));
            }
            
            // Validate SIMD compatibility
            if (TypeInfo.bSupportsSIMD && !IsSIMDInstructionSetSupported(TypeInfo.RequiredInstructionSet))
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type '%s' (ID %u) requires SIMD instruction set that is not supported by the current hardware"),
                    *TypeInfo.TypeName.ToString(), TypeId));
            }
            
            // Validate memory management integration
            IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
            if (MemoryManager && !MemoryManager->GetPoolForType(TypeId))
            {
                ValidationContext.AddError(FString::Printf(TEXT("SVO type '%s' (ID %u) has no memory pool configured"),
                    *TypeInfo.TypeName.ToString(), TypeId));
            }
        },
        Config);
    
    // Check for validation errors
    if (ValidationContext.Errors.Num() > 0)
    {
        OutErrors.Append(ValidationContext.Errors);
        return false;
    }
    
    return true;
}

bool FSVOTypeRegistry::PreInitializeTypes()
{
    if (!IsInitialized())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot pre-initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FSVONodeTypeInfo> AllTypes = GetAllNodeTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to initialize, consider it successful
        return true;
    }
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Pre-initializing %d SVO node types"), AllTypes.Num());
    
    // Perform pre-initialization steps for all types
    for (const FSVONodeTypeInfo& TypeInfo : AllTypes)
    {
        // Ensure memory pool exists for each type
        SynchronizePoolCreation(TypeInfo.TypeId);
    }
    
    return true;
}

bool FSVOTypeRegistry::ParallelInitializeTypes(bool bParallel)
{
    if (!IsInitialized())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FSVONodeTypeInfo> AllTypes = GetAllNodeTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to initialize, consider it successful
        return true;
    }
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Initializing %d SVO node types in %s mode"),
        AllTypes.Num(), bParallel ? TEXT("parallel") : TEXT("sequential"));
    
    // For very small type counts or when parallel is disabled, use sequential initialization
    if (!bParallel || AllTypes.Num() <= 3)
    {
        for (const FSVONodeTypeInfo& TypeInfo : AllTypes)
        {
            // Initialize type-specific resources
            if (TypeInfo.bSupportsMaterialRelationships)
            {
                // Initialize material relationship tables
            }
            
            // Initialize other type-specific resources
        }
        
        return true;
    }
    
    // For larger type counts, use parallel initialization with dependency ordering
    FParallelConfig Config;
    Config.SetExecutionMode(EParallelExecutionMode::ForceParallel);
    
    bool bSuccess = FParallelExecutor::Get().ParallelForWithDependencies(
        AllTypes.Num(),
        [this, &AllTypes](int32 Index)
        {
            const FSVONodeTypeInfo& TypeInfo = AllTypes[Index];
            
            // Initialize type-specific resources
            if (TypeInfo.bSupportsMaterialRelationships)
            {
                // Initialize material relationship tables
            }
            
            // Initialize other type-specific resources
        },
        [this, &AllTypes](int32 Index) -> TArray<int32>
        {
            // Return dependencies for this type
            return GetTypeDependencies(AllTypes[Index].TypeId);
        },
        Config);
    
    return bSuccess;
}

bool FSVOTypeRegistry::PostInitializeTypes()
{
    if (!IsInitialized())
    {
        UE_LOG(LogSVOTypeRegistry, Error, TEXT("Cannot post-initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FSVONodeTypeInfo> AllTypes = GetAllNodeTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to initialize, consider it successful
        return true;
    }
    
    UE_LOG(LogSVOTypeRegistry, Log, TEXT("Post-initializing %d SVO node types"), AllTypes.Num());
    
    // Perform post-initialization steps that require all types to be initialized
    
    // Example: Validate type relationships
    TArray<FString> ValidationErrors;
    bool bSuccess = ValidateTypeConsistency(ValidationErrors, true);
    
    if (!bSuccess)
    {
        for (const FString& Error : ValidationErrors)
        {
            UE_LOG(LogSVOTypeRegistry, Error, TEXT("Post-initialization validation error: %s"), *Error);
        }
        return false;
    }
    
    return true;
}

TArray<int32> FSVOTypeRegistry::GetTypeDependencies(uint32 TypeId) const
{
    TArray<int32> Dependencies;
    
    if (!IsInitialized() || !IsNodeTypeRegistered(TypeId))
    {
        return Dependencies;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Get the type info
    const TSharedRef<FSVONodeTypeInfo>* TypeInfoPtr = NodeTypeMap.Find(TypeId);
    if (!TypeInfoPtr)
    {
        return Dependencies;
    }
    
    const FSVONodeTypeInfo& TypeInfo = TypeInfoPtr->Get();
    
    // Check for dependencies based on node class
    if (TypeInfo.NodeClass == ESVONodeClass::Interface)
    {
        // Interface nodes may depend on the homogeneous node types they can contain
        for (const auto& TypePair : NodeTypeMap)
        {
            if (TypePair.Key != TypeId && TypePair.Value->NodeClass == ESVONodeClass::Homogeneous)
            {
                // Safely convert uint32 to int32, skipping if out of range
                if (TypePair.Key <= static_cast<uint32>(INT32_MAX))
                {
                    Dependencies.Add(static_cast<int32>(TypePair.Key));
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("NodeType ID %u exceeds INT32_MAX, skipping as dependency"), TypePair.Key);
                }
            }
        }
    }
    
    // Add dependencies based on material relationships
    if (TypeInfo.bSupportsMaterialRelationships)
    {
        // Find material type dependencies (would need access to material registry)
    }
    
    return Dependencies;
}

// Add helper method for ParallelInitializeTypes
void FSVOTypeRegistry::InitializeType(uint32 TypeId)
{
    // Implementation would initialize a specific type
    // This is called from ParallelInitializeTypes
}

ETypeCapabilitiesEx FSVOTypeRegistry::GetTypeCapabilitiesEx(uint32 TypeId) const
{
    // Start with no capabilities
    ETypeCapabilitiesEx Capabilities = ETypeCapabilitiesEx::None;
    
    // Check if this type is registered
    if (!IsNodeTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the type info
    const FSVONodeTypeInfo* TypeInfo = GetNodeTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map SVO capabilities to extended type capabilities
    if (TypeInfo->bSupportsSpatialCoherence)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::SpatialCoherence);
    }
    
    if (TypeInfo->bOptimizedMemoryAccess)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::MemoryEfficient);
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::CacheOptimized);
    }
    
    // Determine if this type has low contention characteristics
    if (TypeInfo->bSupportsConcurrentAccess && TypeInfo->MemoryLayout == ESVOMemoryLayout::Interleaved)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::LowContention);
    }
    
    // Determine if this type supports SIMD vectorization
    if (TypeInfo->bSupportsSIMD)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::Vectorizable);
    }
    
    return Capabilities;
}