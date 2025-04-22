// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDFTypeRegistry.h"
#include "HAL/PlatformMisc.h"
#include "MiningSpiceCoPilot/3_ThreadingTaskSystem/Public/TaskHelpers.h"
#include "NumaHelpers.h"
#include "MiningSpiceCoPilot/3_ThreadingTaskSystem/Public/ThreadSafety.h"

// Static singleton instance
FSDFTypeRegistry* FSDFTypeRegistry::Singleton = nullptr;
FThreadSafeBool FSDFTypeRegistry::bSingletonInitialized = false;

// CPU feature support flags
bool bHasSSE2Support = false;
bool bHasAVXSupport = false;
bool bHasAVX2Support = false;
bool bHasAVX512Support = false;
bool bHasGPUSupport = false;

FSDFTypeRegistry::FSDFTypeRegistry()
    : RegistryName(TEXT("SDFType"))
    , SchemaVersion(1)
    , bTypesInitialized(false)
    , bInitializationInProgress(false)
    , bHasGPUSupport(false)
    , bHasSSE2Support(false)
    , bHasAVXSupport(false)
    , bHasAVX2Support(false)
    , bHardwareCapabilitiesDetected(false)
{
    // Registry lock for thread safety
    RegistryLock = MakeShared<FSpinLock>();
    
    // Type counter for generating unique IDs
    NextTypeId.Set(1); // Start at 1, 0 is reserved
    NextOperationId.Set(1);
    
    // Operation and field type maps
    OperationMap = TMap<uint32, TSharedRef<FSDFOperationInfo>>();
    OperationNameMap = TMap<FName, uint32>();
    FieldTypeMap = TMap<uint32, TSharedRef<FSDFFieldTypeInfo>>();
    FieldTypeNameMap = TMap<FName, uint32>();
    TypeBufferMap = TMap<uint32, TSharedRef<FSharedBufferManager>>();
    TypeVersionMap = TMap<uint32, uint32>();
}

FSDFTypeRegistry::~FSDFTypeRegistry()
{
    // Ensure we're properly shut down
    if (bTypesInitialized)
    {
        Shutdown();
    }
}

bool FSDFTypeRegistry::Initialize()
{
    // Initialize the registry lock if not already done
    if (!RegistryLock.IsValid())
    {
        RegistryLock = MakeShared<FSpinLock>();
    }
    
    // Check if we're already initialized
    if (bTypesInitialized)
    {
        return true;
    }
    
    // Set registry name and schema version
    RegistryName = TEXT("SDF_Type_Registry");
    SchemaVersion = 1;
    
    // Allocate initial capacity for type maps
    FieldTypeMap.Reserve(32);
    FieldTypeNameMap.Reserve(32);
    
    // Allocate initial capacity for operation maps
    OperationMap.Reserve(16);
    OperationNameMap.Reserve(16);
    
    // Detect hardware capabilities
    DetectHardwareCapabilities();
    
    // Mark initialized
    bTypesInitialized = true;
    bInitializationInProgress = false;
    
    return true;
}

void FSDFTypeRegistry::Shutdown()
{
    if (bTypesInitialized)
    {
        // Lock for thread safety
        FScopedSpinLock Lock(*RegistryLock);
        
        // Clear all registered items
        FieldTypeMap.Empty();
        FieldTypeNameMap.Empty();
        OperationMap.Empty();
        OperationNameMap.Empty();
        
        // Reset state
        bTypesInitialized = false;
    }
}

bool FSDFTypeRegistry::IsInitialized() const
{
    return bTypesInitialized;
}

FName FSDFTypeRegistry::GetRegistryName() const
{
    return FName(TEXT("SDFTypeRegistry"));
}

uint32 FSDFTypeRegistry::GetSchemaVersion() const
{
    return SchemaVersion;
}

bool FSDFTypeRegistry::Validate(TArray<FString>& OutErrors) const
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("SDF Type Registry is not initialized"));
        return false;
    }
    
    // Check if hardware capabilities have been detected
    if (!bHardwareCapabilitiesDetected)
    {
        // Detect hardware capabilities if not already done
        const_cast<FSDFTypeRegistry*>(this)->DetectHardwareCapabilities();
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    bool bIsValid = true;
    
    // Validate field type map integrity
    for (const auto& TypeNamePair : FieldTypeNameMap)
    {
        const FName& TypeName = TypeNamePair.Key;
        const uint32 TypeId = TypeNamePair.Value;
        
        if (!FieldTypeMap.Contains(TypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("SDF field type name '%s' references non-existent type ID %u"), *TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        else if (FieldTypeMap[TypeId]->TypeName != TypeName)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF field type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *TypeName.ToString(), TypeId, *FieldTypeMap[TypeId]->TypeName.ToString()));
            bIsValid = false;
        }
    }
    
    // Validate operation map integrity
    for (const auto& OpNamePair : OperationNameMap)
    {
        const FName& OpName = OpNamePair.Key;
        const uint32 OpId = OpNamePair.Value;
        
        if (!OperationMap.Contains(OpId))
        {
            OutErrors.Add(FString::Printf(TEXT("SDF operation name '%s' references non-existent operation ID %u"), *OpName.ToString(), OpId));
            bIsValid = false;
        }
        else if (OperationMap[OpId]->OperationName != OpName)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF operation name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *OpName.ToString(), OpId, *OperationMap[OpId]->OperationName.ToString()));
            bIsValid = false;
        }
    }
    
    // Validate field type properties
    for (const auto& TypeInfoPair : FieldTypeMap)
    {
        const uint32 TypeId = TypeInfoPair.Key;
        const TSharedRef<FSDFFieldTypeInfo>& TypeInfo = TypeInfoPair.Value;
        
        // Validate alignment requirement (must be power of 2)
        if ((TypeInfo->AlignmentRequirement & (TypeInfo->AlignmentRequirement - 1)) != 0)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF field type '%s' (ID %u) has invalid alignment requirement %u (must be power of 2)"),
                *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->AlignmentRequirement));
            bIsValid = false;
        }
        
        // Validate channel count (must be positive)
        if (TypeInfo->ChannelCount == 0)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF field type '%s' (ID %u) has invalid channel count 0"),
                *TypeInfo->TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        
        // Validate SIMD compatibility
        if (TypeInfo->bSupportsSIMD)
        {
            if (TypeInfo->AlignmentRequirement < 16)
            {
                OutErrors.Add(FString::Printf(TEXT("SDF field type '%s' (ID %u) supports SIMD but has insufficient alignment %u (must be at least 16)"),
                    *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->AlignmentRequirement));
                bIsValid = false;
            }
        }
        
        // Validate GPU compatibility
        if (TypeInfo->bSupportsGPU && !bHasGPUSupport)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF field type '%s' (ID %u) supports GPU but hardware doesn't support compute shaders"),
                *TypeInfo->TypeName.ToString(), TypeId));
            // Not marking as invalid since this is a hardware capability issue
        }
    }
    
    // Validate operation properties
    for (const auto& OpInfoPair : OperationMap)
    {
        const uint32 OpId = OpInfoPair.Key;
        const TSharedRef<FSDFOperationInfo>& OpInfo = OpInfoPair.Value;
        
        // Validate input count (must be positive except for special operations)
        if (OpInfo->InputCount == 0)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF operation '%s' (ID %u) has invalid input count 0"),
                *OpInfo->OperationName.ToString(), OpId));
            bIsValid = false;
        }
        
        // Validate GPU compatibility
        if (OpInfo->Properties.bSupportsGPU && !bHasGPUSupport)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF operation '%s' (ID %u) supports GPU but hardware doesn't support compute shaders"),
                *OpInfo->OperationName.ToString(), OpId));
            // Not marking as invalid since this is a hardware capability issue
        }
        
        // Validate SIMD compatibility
        if (OpInfo->Properties.bCanVectorize && !bHasSSE2Support)
        {
            OutErrors.Add(FString::Printf(TEXT("SDF operation '%s' (ID %u) supports SIMD but hardware doesn't support SSE2"),
                *OpInfo->OperationName.ToString(), OpId));
            // Not marking as invalid since this is a hardware capability issue
        }
    }
    
    return bIsValid;
}

void FSDFTypeRegistry::Clear()
{
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Clear all registered items
    FieldTypeMap.Empty();
    FieldTypeNameMap.Empty();
    OperationMap.Empty();
    OperationNameMap.Empty();
    TypeBufferMap.Empty();
    TypeVersionMap.Empty();
    
    // Reset type counters
    NextTypeId.Set(1);
    NextOperationId.Set(1);
    
    UE_LOG(LogTemp, Log, TEXT("FSDFTypeRegistry::Clear - Registry cleared"));
}

bool FSDFTypeRegistry::SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData)
{
    // Check if registry is initialized
    if (!bTypesInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - registry not initialized"));
        return false;
    }
    
    // Check if type exists
    if (!FieldTypeMap.Contains(TypeId))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - type ID %u not found"), TypeId);
        return false;
    }
    
    // Get mutable type info
    TSharedRef<FSDFFieldTypeInfo>& TypeInfo = FieldTypeMap[TypeId];
    
    // If version is the same, nothing to do
    if (TypeInfo->SchemaVersion == NewVersion)
    {
        UE_LOG(LogTemp, Warning, TEXT("Type '%s' is already at version %u"),
            *TypeInfo->TypeName.ToString(), NewVersion);
        return true;
    }
    
    // Store old version for logging
    uint32 OldVersion = TypeInfo->SchemaVersion;
    
    // Update type version
    TypeInfo->SchemaVersion = NewVersion;
    
    UE_LOG(LogTemp, Log, TEXT("Updated type '%s' version from %u to %u"),
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
        UE_LOG(LogTemp, Warning, TEXT("Memory migration skipped for type '%s' - Memory Manager not available"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
    
    // Create memory migration data
    FTypeVersionMigrationInfo MigrationInfo;
    MigrationInfo.TypeId = TypeId;
    MigrationInfo.TypeName = TypeInfo->TypeName;
    MigrationInfo.OldVersion = OldVersion;
    MigrationInfo.NewVersion = NewVersion;
    MigrationInfo.DataSize = TypeInfo->DataSize;
    MigrationInfo.AlignmentRequirement = TypeInfo->AlignmentRequirement;
    
    // Try to get the registered pool for this type
    FName PoolName = FName(*FString::Printf(TEXT("SDFType_%s_Pool"), *TypeInfo->TypeName.ToString()));
    IPoolAllocator* TypePool = MemoryManager->GetPool(PoolName);
    
    if (TypePool)
    {
        // Perform migration through the pool
        bool bMigrationSuccess = TypePool->UpdateTypeVersion(MigrationInfo);
        
        if (bMigrationSuccess)
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully migrated memory for type '%s' from version %u to %u"),
                *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to migrate memory for type '%s' from version %u to %u"),
                *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
        }
        
        return bMigrationSuccess;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Memory migration skipped for type '%s' - Pool not found"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
}

uint32 FSDFTypeRegistry::GetTypeVersion(uint32 TypeId) const
{
    // Check if type exists
    if (!FieldTypeMap.Contains(TypeId))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetTypeVersion - type ID %u not found"), TypeId);
        return 0;
    }
    
    // Get type info
    const TSharedRef<FSDFFieldTypeInfo>& TypeInfo = FieldTypeMap[TypeId];
    
    return TypeInfo->SchemaVersion;
}

uint32 FSDFTypeRegistry::RegisterFieldType(
    const FName& InTypeName,
    uint32 InChannelCount,
    uint32 InAlignmentRequirement,
    bool bInSupportsGPU)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterFieldType failed - registry not initialized"));
        return 0;
    }
    
    if (InTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterFieldType failed - invalid type name"));
        return 0;
    }
    
    if (InChannelCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterFieldType failed - channel count must be positive"));
        return 0;
    }
    
    // Ensure alignment requirement is a power of 2
    if ((InAlignmentRequirement & (InAlignmentRequirement - 1)) != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterFieldType failed - alignment requirement %u must be a power of 2"),
            InAlignmentRequirement);
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Check if type name is already registered
    if (FieldTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FSDFTypeRegistry::RegisterFieldType - type '%s' is already registered"),
            *InTypeName.ToString());
        return 0;
    }
    
    // Generate a unique type ID
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create and populate type info
    TSharedRef<FSDFFieldTypeInfo> TypeInfo = MakeShared<FSDFFieldTypeInfo>();
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->ChannelCount = InChannelCount;
    TypeInfo->SchemaVersion = GetSchemaVersion();
    TypeInfo->AlignmentRequirement = InAlignmentRequirement;
    
    // Determine hardware capabilities
    TypeInfo->bSupportsGPU = bInSupportsGPU && bHasGPUSupport;
    if (TypeInfo->bSupportsGPU)
    {
        TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsGPU);
    }
    
    TypeInfo->bSupportsThreading = true; // Default to true for now
    if (TypeInfo->bSupportsThreading)
    {
        TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsThreading);
    }
    
    // Determine SIMD support based on alignment
    TypeInfo->bSupportsSIMD = (InAlignmentRequirement >= 16) && bHasSSE2Support;
    if (TypeInfo->bSupportsSIMD)
    {
        TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsSIMD);
    }
    
    // Set appropriate SIMD instruction set based on alignment and hardware
    if (InAlignmentRequirement >= 64 && bHasAVX512Support)
    {
        TypeInfo->RequiredInstructionSet = ESIMD_InstructionSet::AVX512;
    }
    else if (InAlignmentRequirement >= 32 && bHasAVX2Support)
    {
        TypeInfo->RequiredInstructionSet = ESIMD_InstructionSet::AVX2;
    }
    else if (InAlignmentRequirement >= 16 && bHasSSE2Support)
    {
        TypeInfo->RequiredInstructionSet = ESIMD_InstructionSet::SSE2;
    }
    else
    {
        TypeInfo->RequiredInstructionSet = ESIMD_InstructionSet::None;
    }
    
    // Memory layout based on channel count
    TypeInfo->MemoryLayout = (InChannelCount > 1) ? ESDFMemoryLayout::Interleaved : ESDFMemoryLayout::Sequential;
    
    // Default to sequential memory access pattern
    TypeInfo->MemoryPattern = ESDFMemoryAccessPattern::Sequential;
    
    // Calculate the data size based on channel count and precision
    TypeInfo->PrecisionMode = ESDFPrecisionMode::SinglePrecision; // Default to single precision for now
    
    // Calculate data size based on precision mode and channel count
    switch (TypeInfo->PrecisionMode)
    {
        case ESDFPrecisionMode::HalfPrecision:
            TypeInfo->DataSize = sizeof(uint16) * InChannelCount;
            break;
        case ESDFPrecisionMode::SinglePrecision:
            TypeInfo->DataSize = sizeof(float) * InChannelCount;
            break;
        case ESDFPrecisionMode::DoublePrecision:
            TypeInfo->DataSize = sizeof(double) * InChannelCount;
            break;
        default:
            TypeInfo->DataSize = sizeof(float) * InChannelCount;
            break;
    }
    
    // Adjust for alignment requirements
    TypeInfo->DataSize = ((TypeInfo->DataSize + TypeInfo->AlignmentRequirement - 1) / TypeInfo->AlignmentRequirement) * TypeInfo->AlignmentRequirement;
    
    // Set versioned serialization support
    TypeInfo->bSupportsVersionedSerialization = true;
    TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsVersionedSerialization);
    
    // Set hot-reload support
    TypeInfo->bSupportsHotReload = true;
    TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsHotReload);
    
    // Set incremental updates support based on channel count (more complex for multi-channel)
    TypeInfo->bSupportsIncrementalUpdates = (InChannelCount == 1);
    if (TypeInfo->bSupportsIncrementalUpdates)
    {
        TypeInfo->AddCapability(ESDFFieldCapabilities::SupportsIncrementalUpdates);
    }
    
    // Register the type
    FieldTypeMap.Add(TypeId, TypeInfo);
    FieldTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FSDFTypeRegistry::RegisterFieldType - registered type '%s' with ID %u"),
        *InTypeName.ToString(), TypeId);
    
    return TypeId;
}

uint32 FSDFTypeRegistry::RegisterOperation(
    const FName& InOperationName,
    ESDFOperationType InOperationType,
    uint32 InInputCount,
    bool bInSupportsSmoothing)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot register operation - registry not initialized"));
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Check if operation name already exists
    if (OperationNameMap.Contains(InOperationName))
    {
        UE_LOG(LogTemp, Warning, TEXT("Operation with name '%s' already registered"), *InOperationName.ToString());
        return OperationNameMap[InOperationName];
    }
    
    // Generate unique operation ID
    uint32 OperationId = GenerateUniqueOperationId();
    
    // Create operation info
    TSharedRef<FSDFOperationInfo> OperationInfo = MakeShared<FSDFOperationInfo>();
    OperationInfo->OperationId = OperationId;
    OperationInfo->OperationName = InOperationName;
    OperationInfo->OperationType = InOperationType;
    OperationInfo->InputCount = InInputCount;
    OperationInfo->bSupportsSmoothing = bInSupportsSmoothing;
    
    // Set default operation properties based on type
    SetDefaultOperationProperties(&OperationInfo.Get(), InOperationType);
    
    // Check GPU and SIMD compatibility
    OperationInfo->Properties.bSupportsGPU = IsOperationGPUCompatible(InOperationType);
    OperationInfo->Properties.bCanVectorize = IsOperationSIMDCompatible(InOperationType);
    
    // Set preferred thread block size
    OperationInfo->Properties.PreferredThreadBlockSize = GetOptimalThreadBlockSize(InOperationType);
    
    // Add to maps
    OperationMap.Add(OperationId, OperationInfo);
    OperationNameMap.Add(InOperationName, OperationId);
    
    return OperationId;
}

uint32 FSDFTypeRegistry::RegisterFieldOperation(
    const FName& InOperationName,
    ESDFOperationType InOperationType,
    uint32 InInputCount,
    bool bInSupportsSmoothing)
{
    // This is just a wrapper around the RegisterOperation function
    return RegisterOperation(InOperationName, InOperationType, InInputCount, bInSupportsSmoothing);
}

const FSDFFieldTypeInfo* FSDFTypeRegistry::GetFieldTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    if (const TSharedRef<FSDFFieldTypeInfo>* TypeInfoRef = FieldTypeMap.Find(InTypeId))
    {
        // Check if this type has an associated buffer
        if (!TypeBufferMap.Contains(InTypeId))
        {
            // Create the buffer only if needed - using mutable this is a bit of a hack 
            // but it allows us to maintain const correctness on the API
            FSDFTypeRegistry* MutableThis = const_cast<FSDFTypeRegistry*>(this);
            
            // Get memory layout value for the type
            uint32 MemoryLayoutValue = static_cast<uint32>((*TypeInfoRef)->MemoryLayout);
            
            // Create a shared buffer for this field type
            TSharedPtr<FSharedBufferManager> TypedBuffer = FSharedBufferManager::CreateTypedBuffer(
                (*TypeInfoRef)->TypeName,
                InTypeId,
                (*TypeInfoRef)->DataSize,
                (*TypeInfoRef)->AlignmentRequirement,
                (*TypeInfoRef)->bSupportsGPU,
                MemoryLayoutValue,
                (*TypeInfoRef)->CapabilitiesFlags
            );
            
            if (TypedBuffer.IsValid())
            {
                MutableThis->TypeBufferMap.Add(InTypeId, TypedBuffer.ToSharedRef());
                UE_LOG(LogTemp, Log, TEXT("Created type-safe buffer for field type '%s'"), *(*TypeInfoRef)->TypeName.ToString());
            }
        }
        
        return &(*TypeInfoRef).Get();
    }
    
    return nullptr;
}

const FSDFFieldTypeInfo* FSDFTypeRegistry::GetFieldTypeInfoByName(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = FieldTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        const TSharedRef<FSDFFieldTypeInfo>* TypeInfoPtr = FieldTypeMap.Find(*TypeIdPtr);
        if (TypeInfoPtr)
        {
            return &(TypeInfoPtr->Get());
        }
    }
    
    return nullptr;
}

const FSDFOperationInfo* FSDFTypeRegistry::GetOperationInfo(uint32 InOperationId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Look up operation by ID
    const TSharedRef<FSDFOperationInfo>* OpInfoPtr = OperationMap.Find(InOperationId);
    if (OpInfoPtr)
    {
        return &(OpInfoPtr->Get());
    }
    
    return nullptr;
}

const FSDFOperationInfo* FSDFTypeRegistry::GetOperationInfoByName(const FName& InOperationName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Look up operation ID by name
    const uint32* OpIdPtr = OperationNameMap.Find(InOperationName);
    if (OpIdPtr)
    {
        // Look up operation info by ID
        const TSharedRef<FSDFOperationInfo>* OpInfoPtr = OperationMap.Find(*OpIdPtr);
        if (OpInfoPtr)
        {
            return &(OpInfoPtr->Get());
        }
    }
    
    return nullptr;
}

TArray<FSDFFieldTypeInfo> FSDFTypeRegistry::GetAllFieldTypes() const
{
    TArray<FSDFFieldTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Collect all field type infos
    Result.Reserve(FieldTypeMap.Num());
    for (const auto& TypeInfoPair : FieldTypeMap)
    {
        Result.Add(TypeInfoPair.Value.Get());
    }
    
    return Result;
}

TArray<FSDFOperationInfo> FSDFTypeRegistry::GetAllOperations() const
{
    TArray<FSDFOperationInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Collect all operation infos
    Result.Reserve(OperationMap.Num());
    for (const auto& OpInfoPair : OperationMap)
    {
        Result.Add(OpInfoPair.Value.Get());
    }
    
    return Result;
}

TArray<FSDFOperationInfo> FSDFTypeRegistry::GetOperationsByType(ESDFOperationType InOperationType) const
{
    TArray<FSDFOperationInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Collect operations matching the specified type
    for (const auto& OpInfoPair : OperationMap)
    {
        const FSDFOperationInfo& OpInfo = OpInfoPair.Value.Get();
        if (OpInfo.OperationType == InOperationType)
        {
            Result.Add(OpInfo);
        }
    }
    
    return Result;
}

TArray<FSDFFieldTypeInfo> FSDFTypeRegistry::GetFieldTypesWithCapability(ESDFFieldCapabilities InCapability) const
{
    TArray<FSDFFieldTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Collect field types with the specified capability
    for (const auto& TypeInfoPair : FieldTypeMap)
    {
        const FSDFFieldTypeInfo& TypeInfo = TypeInfoPair.Value.Get();
        if (TypeInfo.HasCapability(InCapability))
        {
            Result.Add(TypeInfo);
        }
    }
    
    return Result;
}

bool FSDFTypeRegistry::IsFieldTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Check if the type exists in the name map
    return FieldTypeNameMap.Contains(InTypeName);
}

bool FSDFTypeRegistry::IsFieldTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    // Check if the type exists
    return FieldTypeMap.Contains(InTypeId);
}

bool FSDFTypeRegistry::IsOperationRegistered(uint32 InOperationId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    return OperationMap.Contains(InOperationId);
}

bool FSDFTypeRegistry::IsOperationRegistered(const FName& InOperationName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    return OperationNameMap.Contains(InOperationName);
}

bool FSDFTypeRegistry::IsOperationGPUCompatible(ESDFOperationType InOperationType) const
{
    // Union and intersection operations are well-suited for GPU
    // Smooth operations are also GPU-friendly but may require more resources
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
        case ESDFOperationType::Intersection:
        case ESDFOperationType::SmoothUnion:
        case ESDFOperationType::SmoothIntersection:
            return true;
            
        case ESDFOperationType::Subtraction:
        case ESDFOperationType::SmoothSubtraction:
            return true; // Can be implemented efficiently on GPU
            
        case ESDFOperationType::Custom:
            return false; // Need more info about custom op
            
        default:
            return false;
    }
}

bool FSDFTypeRegistry::IsOperationSIMDCompatible(ESDFOperationType InOperationType) const
{
    // Most SDF operations are SIMD-friendly
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
        case ESDFOperationType::Intersection:
        case ESDFOperationType::Subtraction:
            return true; // Very SIMD-friendly
            
        case ESDFOperationType::SmoothUnion:
        case ESDFOperationType::SmoothIntersection:
        case ESDFOperationType::SmoothSubtraction:
            return true; // SIMD-friendly but more complex
            
        case ESDFOperationType::Custom:
            return false; // Need more info about custom op
            
        default:
            return false;
    }
}

uint32 FSDFTypeRegistry::GetOptimalThreadBlockSize(ESDFOperationType InOperationType) const
{
    // Different operations have different optimal thread block sizes
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
        case ESDFOperationType::Intersection:
        case ESDFOperationType::Subtraction:
            return 128; // Simple operations can use larger blocks
            
        case ESDFOperationType::SmoothUnion:
        case ESDFOperationType::SmoothIntersection:
        case ESDFOperationType::SmoothSubtraction:
            return 64; // More complex operations use smaller blocks
            
        case ESDFOperationType::Custom:
            return 32; // Conservative for custom ops
            
        default:
            return 64; // Default size
    }
}

FSDFTypeRegistry& FSDFTypeRegistry::Get()
{
    if (!Singleton)
    {
        Singleton = new FSDFTypeRegistry();
    }
    
    return *Singleton;
}

uint32 FSDFTypeRegistry::GenerateUniqueTypeId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextTypeId.Increment();
}

uint32 FSDFTypeRegistry::GenerateUniqueOperationId()
{
    return NextOperationId.Increment();
}

void FSDFTypeRegistry::DetectHardwareCapabilities()
{
    // Skip if we've already detected hardware capabilities
    if (bHardwareCapabilitiesDetected)
    {
        return;
    }
    
    // Detect GPU capabilities
    bHasGPUSupport = true; // Replace with actual GPU detection logic
    
    // Detect CPU capabilities for SIMD
    bHasSSE2Support = true; // Replace with actual SSE2 detection
    bHasAVXSupport = true;  // Replace with actual AVX detection
    bHasAVX2Support = false; // Replace with actual AVX2 detection
    
    // Mark as detected
    bHardwareCapabilitiesDetected = true;
}

void FSDFTypeRegistry::SetDefaultOperationProperties(FSDFOperationInfo* OpInfo, ESDFOperationType InOperationType)
{
    if (!OpInfo)
    {
        return;
    }
    
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 1.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::High;
            break;
            
        case ESDFOperationType::Subtraction:
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 1.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::High;
            break;
            
        case ESDFOperationType::Intersection:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 1.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::High;
            break;
            
        case ESDFOperationType::SmoothUnion:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 2.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::Medium;
            break;
            
        case ESDFOperationType::SmoothSubtraction:
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 2.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::Medium;
            break;
            
        case ESDFOperationType::SmoothIntersection:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            OpInfo->Properties.bCanVectorize = true;
            OpInfo->Properties.EvaluationCost = 2.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Sequential;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::Medium;
            break;
            
        case ESDFOperationType::Custom:
            // For custom operations, use conservative defaults
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            OpInfo->Properties.bCanVectorize = false;
            OpInfo->Properties.EvaluationCost = 3.0f;
            OpInfo->Properties.MemoryPattern = ESDFMemoryAccessPattern::Random;
            OpInfo->Properties.CacheLocality = ESDFCacheLocality::Low;
            break;
    }
    
    // Set optimization level based on operation type
    if (InOperationType == ESDFOperationType::Custom)
    {
        OpInfo->Properties.OptimizationLevel = ESDFOptimizationLevel::Conservative;
    }
    else if (OpInfo->bSupportsSmoothing)
    {
        OpInfo->Properties.OptimizationLevel = ESDFOptimizationLevel::Default;
    }
    else
    {
        OpInfo->Properties.OptimizationLevel = ESDFOptimizationLevel::Aggressive;
    }
    
    // GPU shader name based on operation type
    FString ShaderName;
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
            ShaderName = TEXT("SDFUnion");
            break;
        case ESDFOperationType::Intersection:
            ShaderName = TEXT("SDFIntersection");
            break;
        case ESDFOperationType::Subtraction:
            ShaderName = TEXT("SDFSubtraction");
            break;
        case ESDFOperationType::SmoothUnion:
            ShaderName = TEXT("SDFSmoothUnion");
            break;
        case ESDFOperationType::SmoothIntersection:
            ShaderName = TEXT("SDFSmoothIntersection");
            break;
        case ESDFOperationType::SmoothSubtraction:
            ShaderName = TEXT("SDFSmoothSubtraction");
            break;
        default:
            ShaderName = TEXT("SDFCustom");
            break;
    }
    
    OpInfo->Properties.GPUShaderName = FName(*ShaderName);
}

ERegistryType FSDFTypeRegistry::GetRegistryType() const
{
    return ERegistryType::SDF;
}

ETypeCapabilities FSDFTypeRegistry::GetTypeCapabilities(uint32 TypeId) const
{
    // Start with no capabilities
    ETypeCapabilities Capabilities = ETypeCapabilities::None;
    
    // Check if this type is registered
    if (!IsFieldTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the field type info
    const FSDFFieldTypeInfo* TypeInfo = GetFieldTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map SDF capabilities to type capabilities
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
    
    return Capabilities;
}

ETypeCapabilitiesEx FSDFTypeRegistry::GetTypeCapabilitiesEx(uint32 TypeId) const
{
    // Start with no extended capabilities
    ETypeCapabilitiesEx Capabilities = ETypeCapabilitiesEx::None;
    
    // Check if this type is registered
    if (!IsFieldTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the field type info
    const FSDFFieldTypeInfo* TypeInfo = GetFieldTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map SDF capabilities to extended type capabilities
    if (TypeInfo->bSupportsGPU)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::Vectorizable);
    }
    
    if (TypeInfo->bOptimizedAccess)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::CacheOptimized);
    }
    
    return Capabilities;
}

uint64 FSDFTypeRegistry::ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config)
{
    // Create a type-specific task configuration
    FTaskConfig TypedConfig = Config;
    TypedConfig.SetTypeId(TypeId, ERegistryType::SDF);
    
    // Set optimization flags based on type capabilities
    ETypeCapabilities BasicCapabilities = GetTypeCapabilities(TypeId);
    ETypeCapabilitiesEx ExtendedCapabilities = GetTypeCapabilitiesEx(TypeId);
    EThreadOptimizationFlags OptimizationFlags = FTaskScheduler::MapCapabilitiesToOptimizationFlags(
        BasicCapabilities, ExtendedCapabilities);
    
    TypedConfig.SetOptimizationFlags(OptimizationFlags);
    
    // Schedule the task with the scheduler
    return ScheduleTaskWithScheduler(TaskFunc, TypedConfig);
}

bool FSDFTypeRegistry::PreInitializeTypes()
{
    // Prepare fields for initialization
    // Allocate memory and initialize basic structures
    FScopedSpinLock Lock(*RegistryLock);
    
    // Reset initialization flags
    bTypesInitialized = false;
    bInitializationInProgress = true;
    
    // Clear any existing errors
    InitializationErrors.Empty();
    
    return true;
}

bool FSDFTypeRegistry::ParallelInitializeTypes(bool bParallel)
{
    if (!bParallel)
    {
        // Sequential fallback
        FScopedSpinLock Lock(*RegistryLock);
        for (const auto& TypePair : FieldTypeMap)
        {
            // Initialize each type sequentially
            InitializeFieldType(TypePair.Key);
        }
        return true;
    }
    
    // Get all type IDs
    TArray<uint32> TypeIds;
    {
        FScopedSpinLock Lock(*RegistryLock);
        FieldTypeMap.GenerateKeyArray(TypeIds);
    }
    
    // Execute initialization in parallel with dependencies
    return FParallelExecutor::Get().ParallelForWithDependencies(
        TypeIds.Num(),
        [this, &TypeIds](int32 Index) {
            InitializeFieldType(TypeIds[Index]);
        },
        [this, &TypeIds](int32 Index) {
            return GetTypeDependencies(TypeIds[Index]);
        },
        FParallelConfig().SetExecutionMode(EParallelExecutionMode::Automatic)
    );
}

bool FSDFTypeRegistry::PostInitializeTypes()
{
    FScopedSpinLock Lock(*RegistryLock);
    
    // Perform final validation and setup
    TArray<FString> ValidationErrors;
    bool bValidationSuccess = Validate(ValidationErrors);
    
    if (!bValidationSuccess)
    {
        InitializationErrors.Append(ValidationErrors);
    }
    
    // Mark initialization as complete
    bInitializationInProgress = false;
    bTypesInitialized = InitializationErrors.Num() == 0;
    
    return bTypesInitialized;
}

TArray<int32> FSDFTypeRegistry::GetTypeDependencies(uint32 TypeId) const
{
    // Lock for thread safety
    FScopedSpinLock Lock(*RegistryLock);
    
    TArray<int32> Dependencies;
    // Add implementation-specific dependencies logic here
    return Dependencies;
}

void FSDFTypeRegistry::InitializeFieldType(uint32 TypeId)
{
    FScopedSpinLock Lock(*RegistryLock);
    
    const TSharedRef<FSDFFieldTypeInfo>* TypeInfoPtr = FieldTypeMap.Find(TypeId);
    
    if (!TypeInfoPtr)
    {
        return;
    }
    
    // Implementation would initialize this specific field type
    // For example: set up memory management, calculate processing parameters, etc.
}