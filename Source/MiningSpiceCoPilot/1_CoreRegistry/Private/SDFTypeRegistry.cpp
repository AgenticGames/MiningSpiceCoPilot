// Copyright Epic Games, Inc. All Rights Reserved.

#include "1_CoreRegistry/Public/SDFTypeRegistry.h"

// Initialize static members
FSDFTypeRegistry* FSDFTypeRegistry::Singleton = nullptr;
FThreadSafeBool FSDFTypeRegistry::bSingletonInitialized = false;

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
    bool bWasAlreadyInitialized = false;
    if (!bIsInitialized.AtomicSet(true, bWasAlreadyInitialized))
    {
        // Initialize internal maps
        FieldTypeMap.Empty();
        FieldTypeNameMap.Empty();
        OperationMap.Empty();
        OperationNameMap.Empty();
        
        // Reset counters
        NextTypeId = 1;
        NextOperationId = 1;
        
        return true;
    }
    
    // Already initialized
    return false;
}

void FSDFTypeRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopeLock Lock(&RegistryLock);
        
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
    FScopeLock Lock(&RegistryLock);
    
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
    }
    
    return bIsValid;
}

void FSDFTypeRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety
        FScopeLock Lock(&RegistryLock);
        
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
    FScopeLock Lock(&RegistryLock);
    
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
    TypeInfo->bSupportsGPU = bInSupportsGPU;
    TypeInfo->bSupportsThreading = true; // Default to true for now
    TypeInfo->bSupportsSIMD = (InAlignmentRequirement >= 16); // Assume SIMD support if aligned to 16+ bytes
    
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
    FScopeLock Lock(&RegistryLock);
    
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
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            break;
            
        case ESDFOperationType::Subtraction:
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            break;
            
        case ESDFOperationType::Intersection:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            break;
            
        case ESDFOperationType::SmoothUnion:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            break;
            
        case ESDFOperationType::SmoothSubtraction:
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            break;
            
        case ESDFOperationType::SmoothIntersection:
            OpInfo->bPreservesSign = true;
            OpInfo->bIsCommutative = true;
            break;
            
        case ESDFOperationType::Custom:
            // For custom operations, use conservative defaults
            OpInfo->bPreservesSign = false;
            OpInfo->bIsCommutative = false;
            break;
    }
    
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up type by ID
    TSharedRef<FSDFFieldTypeInfo>* TypeInfoPtr = FieldTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        return &(TypeInfoPtr->Get());
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = FieldTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        TSharedRef<FSDFFieldTypeInfo>* TypeInfoPtr = FieldTypeMap.Find(*TypeIdPtr);
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up operation by ID
    TSharedRef<FSDFOperationInfo>* OpInfoPtr = OperationMap.Find(InOperationId);
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up operation ID by name
    const uint32* OpIdPtr = OperationNameMap.Find(InOperationName);
    if (OpIdPtr)
    {
        // Look up operation info by ID
        TSharedRef<FSDFOperationInfo>* OpInfoPtr = OperationMap.Find(*OpIdPtr);
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
    FScopeLock Lock(&RegistryLock);
    
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
    FScopeLock Lock(&RegistryLock);
    
    // Collect all operation infos
    Result.Reserve(OperationMap.Num());
    for (const auto& OpInfoPair : OperationMap)
    {
        Result.Add(OpInfoPair.Value.Get());
    }
    
    return Result;
}

bool FSDFTypeRegistry::IsFieldTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    return FieldTypeMap.Contains(InTypeId);
}

bool FSDFTypeRegistry::IsOperationRegistered(uint32 InOperationId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    return OperationMap.Contains(InOperationId);
}

FSDFTypeRegistry& FSDFTypeRegistry::Get()
{
    if (!bSingletonInitialized)
    {
        bool bWasAlreadyInitialized = false;
        if (!bSingletonInitialized.AtomicSet(true, bWasAlreadyInitialized))
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