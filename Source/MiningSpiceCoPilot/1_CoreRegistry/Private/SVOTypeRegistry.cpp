// Copyright Epic Games, Inc. All Rights Reserved.

#include "1_CoreRegistry/Public/SVOTypeRegistry.h"

// Initialize static members
FSVOTypeRegistry* FSVOTypeRegistry::Singleton = nullptr;
FThreadSafeBool FSVOTypeRegistry::bSingletonInitialized = false;

FSVOTypeRegistry::FSVOTypeRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , bIsInitialized(false)
    , SchemaVersion(1) // Initial schema version
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
    bool bWasAlreadyInitialized = false;
    if (!bIsInitialized.AtomicSet(true, bWasAlreadyInitialized))
    {
        // Initialize internal maps
        NodeTypeMap.Empty();
        NodeTypeNameMap.Empty();
        
        // Reset type ID counter
        NextTypeId = 1;
        
        return true;
    }
    
    // Already initialized
    return false;
}

void FSVOTypeRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopeLock Lock(&RegistryLock);
        
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
    FScopeLock Lock(&RegistryLock);
    
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
    }
    
    return bIsValid;
}

void FSVOTypeRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety
        FScopeLock Lock(&RegistryLock);
        
        // Clear all registered types
        NodeTypeMap.Empty();
        NodeTypeNameMap.Empty();
        
        // Reset type ID counter
        NextTypeId = 1;
    }
}

uint32 FSVOTypeRegistry::RegisterNodeType(
    const FName& InTypeName,
    ESVONodeClass InNodeClass,
    uint32 InDataSize,
    uint32 InAlignmentRequirement,
    bool bInSupportsMaterialRelationships)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FSVOTypeRegistry::RegisterNodeType failed - registry not initialized"));
        return 0;
    }
    
    if (InTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FSVOTypeRegistry::RegisterNodeType failed - invalid type name"));
        return 0;
    }
    
    if (InDataSize == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FSVOTypeRegistry::RegisterNodeType failed - data size cannot be zero"));
        return 0;
    }
    
    // Ensure alignment requirement is a power of 2
    if ((InAlignmentRequirement & (InAlignmentRequirement - 1)) != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FSVOTypeRegistry::RegisterNodeType failed - alignment requirement %u must be a power of 2"),
            InAlignmentRequirement);
        return 0;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    // Check if type name is already registered
    if (NodeTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FSVOTypeRegistry::RegisterNodeType - type '%s' is already registered"),
            *InTypeName.ToString());
        return 0;
    }
    
    // Generate a unique type ID
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create and populate type info
    TSharedRef<FSVONodeTypeInfo> TypeInfo = MakeShared<FSVONodeTypeInfo>();
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->NodeClass = InNodeClass;
    TypeInfo->SchemaVersion = GetSchemaVersion();
    TypeInfo->AlignmentRequirement = InAlignmentRequirement;
    TypeInfo->DataSize = InDataSize;
    TypeInfo->bSupportsMaterialRelationships = bInSupportsMaterialRelationships;
    TypeInfo->bSupportsSerializaton = true; // Default to true for now
    TypeInfo->bSupportsSIMD = (InAlignmentRequirement >= 16); // Assume SIMD support if aligned to 16+ bytes
    
    // Register the type
    NodeTypeMap.Add(TypeId, TypeInfo);
    NodeTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FSVOTypeRegistry::RegisterNodeType - registered type '%s' with ID %u"),
        *InTypeName.ToString(), TypeId);
    
    return TypeId;
}

const FSVONodeTypeInfo* FSVOTypeRegistry::GetNodeTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    // Look up type by ID
    TSharedRef<FSVONodeTypeInfo>* TypeInfoPtr = NodeTypeMap.Find(InTypeId);
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = NodeTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        TSharedRef<FSVONodeTypeInfo>* TypeInfoPtr = NodeTypeMap.Find(*TypeIdPtr);
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
    FScopeLock Lock(&RegistryLock);
    
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
    FScopeLock Lock(&RegistryLock);
    
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
    FScopeLock Lock(&RegistryLock);
    
    return NodeTypeMap.Contains(InTypeId);
}

bool FSVOTypeRegistry::IsNodeTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    return NodeTypeNameMap.Contains(InTypeName);
}

FSVOTypeRegistry& FSVOTypeRegistry::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        bool bWasAlreadyInitialized = false;
        if (!bSingletonInitialized.AtomicSet(true, bWasAlreadyInitialized))
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
    return NextTypeId++;
}