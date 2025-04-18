// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDFTypeRegistry.h"
#include "HAL/PlatformMisc.h"

// Initialize static members
FSDFTypeRegistry* FSDFTypeRegistry::Singleton = nullptr;
FThreadSafeBool FSDFTypeRegistry::bSingletonInitialized = false;

// CPU feature support flags
bool bHasSSE2Support = false;
bool bHasAVXSupport = false;
bool bHasAVX2Support = false;
bool bHasAVX512Support = false;
bool bHasGPUSupport = false;

FSDFTypeRegistry::FSDFTypeRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , NextOperationId(1) // Reserve 0 as invalid/unregistered operation ID
    , bIsInitialized(false)
    , SchemaVersion(1) // Initial schema version
{
    // Constructor is intentionally minimal
}

FSDFTypeRegistry::~FSDFTypeRegistry()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FSDFTypeRegistry::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return false;
    }
    
    // Set initialized flag
    bIsInitialized.AtomicSet(true);
    
    // Initialize internal maps
    FieldTypeMap.Empty();
    FieldTypeNameMap.Empty();
    OperationMap.Empty();
    OperationNameMap.Empty();
    
    // Reset type ID counter
    NextTypeId = 1;
    NextOperationId = 1;
    
    // Detect CPU capabilities
    DetectHardwareCapabilities();
    
    return true;
}

void FSDFTypeRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FRWScopeLock Lock(RegistryLock, SLT_Write);
        
        // Clear all registered items
        FieldTypeMap.Empty();
        FieldTypeNameMap.Empty();
        OperationMap.Empty();
        OperationNameMap.Empty();
        
        // Reset state
        bIsInitialized = false;
    }
}

bool FSDFTypeRegistry::IsInitialized() const
{
    return bIsInitialized;
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
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    if (IsInitialized())
    {
        // Lock for thread safety
        FRWScopeLock Lock(RegistryLock, SLT_Write);
        
        // Clear all registered items
        FieldTypeMap.Empty();
        FieldTypeNameMap.Empty();
        OperationMap.Empty();
        OperationNameMap.Empty();
        
        // Reset counters
        NextTypeId = 1;
        NextOperationId = 1;
    }
}

bool FSDFTypeRegistry::SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData)
{
    // Check if registry is initialized
    if (!bIsInitialized)
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
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
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
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterOperation failed - registry not initialized"));
        return 0;
    }
    
    if (InOperationName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterOperation failed - invalid operation name"));
        return 0;
    }
    
    if (InInputCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FSDFTypeRegistry::RegisterOperation failed - input count must be positive"));
        return 0;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
    // Check if operation name is already registered
    if (OperationNameMap.Contains(InOperationName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FSDFTypeRegistry::RegisterOperation - operation '%s' is already registered"),
            *InOperationName.ToString());
        return 0;
    }
    
    // Generate a unique operation ID
    uint32 OperationId = GenerateUniqueOperationId();
    
    // Create and populate operation info
    TSharedRef<FSDFOperationInfo> OpInfo = MakeShared<FSDFOperationInfo>();
    OpInfo->OperationId = OperationId;
    OpInfo->OperationName = InOperationName;
    OpInfo->OperationType = InOperationType;
    OpInfo->InputCount = InInputCount;
    OpInfo->bSupportsSmoothing = bInSupportsSmoothing;
    
    // Set operation properties based on type
    SetDefaultOperationProperties(&OpInfo.Get(), InOperationType);
    
    // Set hardware-dependent properties
    OpInfo->Properties.bSupportsGPU = bHasGPUSupport && IsOperationGPUCompatible(InOperationType);
    OpInfo->Properties.bCanVectorize = bHasSSE2Support && IsOperationSIMDCompatible(InOperationType);
    
    // Set optimal thread block size based on operation type
    OpInfo->Properties.PreferredThreadBlockSize = GetOptimalThreadBlockSize(InOperationType);
    
    // Register the operation
    OperationMap.Add(OperationId, OpInfo);
    OperationNameMap.Add(InOperationName, OperationId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FSDFTypeRegistry::RegisterOperation - registered operation '%s' with ID %u"),
        *InOperationName.ToString(), OperationId);
    
    return OperationId;
}

const FSDFFieldTypeInfo* FSDFTypeRegistry::GetFieldTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
                MutableThis->TypeBufferMap.Add(InTypeId, TypedBuffer);
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
    return OperationMap.Contains(InOperationId);
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
    if (!bSingletonInitialized)
    {
        if (!bSingletonInitialized.AtomicSet(true))
        {
            Singleton = new FSDFTypeRegistry();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

uint32 FSDFTypeRegistry::GenerateUniqueTypeId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextTypeId++;
}

uint32 FSDFTypeRegistry::GenerateUniqueOperationId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextOperationId++;
}

void FSDFTypeRegistry::DetectHardwareCapabilities()
{
    // Set default values based on compile-time platform capabilities
#if PLATFORM_ENABLE_VECTORINTRINSICS
    bHasSSE2Support = true; // SSE2 is guaranteed on x86-64 platforms that support vector intrinsics
#else
    bHasSSE2Support = false;
#endif

    // Check for AVX capabilities - conservative assumptions
    bHasAVXSupport = false;
    bHasAVX2Support = false;
    bHasAVX512Support = false;
    
    // Check GPU compute shader support - use compile-time check
#if WITH_EDITOR || WITH_EDITORONLY_DATA
    bHasGPUSupport = true; // For editor builds, assume it's supported
#else
    // In shipping builds, we could do a more detailed check if needed
    bHasGPUSupport = true;
#endif
    
    // Log the hardware detection results with version details
    UE_LOG(LogTemp, Log, TEXT("SDF hardware capabilities detected: SSE2=%d, AVX=%d, AVX2=%d, AVX512=%d"),
        bHasSSE2Support, bHasAVXSupport, bHasAVX2Support, bHasAVX512Support);
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