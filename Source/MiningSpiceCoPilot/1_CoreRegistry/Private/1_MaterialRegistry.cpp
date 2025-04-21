// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialRegistry.h"
#include "Logging/LogMacros.h"
#include "HAL/PlatformProcess.h"

// Add memory management includes
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IServiceLocator.h"
#include "NarrowBandAllocator.h"
#include "CompressionUtility.h"
#include "Misc/ScopeLock.h"
#include "UObject/UObjectGlobals.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Interfaces/ITaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/TaskSystem/TaskTypes.h"
#include "../../3_ThreadingTaskSystem/Public/TaskHelpers.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "ThreadSafety.h"

DEFINE_LOG_CATEGORY_STATIC(LogMaterialRegistry, Log, All);

// Helper function to mimic TMultiMap's MultiFind functionality for regular TMap with array values
template<typename KeyType, typename ValueType, typename Allocator>
void MultiFind(const TMap<KeyType, TArray<ValueType, Allocator>>& Map, const KeyType& Key, TArray<ValueType>& OutValues)
{
    const TArray<ValueType, Allocator>* FoundValues = Map.Find(Key);
    if (FoundValues)
    {
        OutValues = *FoundValues;
    }
    else
    {
        OutValues.Empty();
    }
}

// Initialize static members
FMaterialRegistry* FMaterialRegistry::Singleton = nullptr;
FThreadSafeBool FMaterialRegistry::bSingletonInitialized = false;

/**
 * Helper function to schedule a task using the task scheduler
 * This function is used by registry implementations to avoid circular dependencies
 * 
 * @param TaskFunc The task function to execute
 * @param Config Configuration for the task
 * @return Task ID of the scheduled task
 */

// Singleton accessor implementation
FMaterialRegistry& FMaterialRegistry::Get()
{
    if (!bSingletonInitialized)
    {
        // Create the singleton instance if it doesn't exist
        FMaterialRegistry* NewInstance = new FMaterialRegistry();
        // Atomically set the instance pointer
        if (FPlatformAtomics::InterlockedCompareExchangePointer(
            (void**)&Singleton, 
            NewInstance, 
            nullptr) != nullptr)
        {
            // Another thread beat us to it, delete our instance
            delete NewInstance;
        }
        else
        {
            // We successfully set the instance
            bSingletonInitialized = true;
        }
    }
    
    check(Singleton);
    return *Singleton;
}

FMaterialRegistry::FMaterialRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , NextRelationshipId(1) // Reserve 0 as invalid/unregistered relationship ID
    , CurrentSchemaVersion(1) // Initial schema version
    , NextChannelId(0)
{
    // Constructor is intentionally minimal
}

FMaterialRegistry::~FMaterialRegistry()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FMaterialRegistry::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return false;
    }
    
    // Set initialized flag
    bIsInitialized = true;
    
    // Initialize internal maps
    MaterialTypes.Empty();
    MaterialTypeNameMap.Empty();
    RelationshipMap.Empty();
    MaterialTypeHierarchy.Empty();
    MaterialPropertyMap.Empty();
    
    // Initialize counters with proper values
    // In FThreadSafeCounter, we need to set the initial value correctly
    NextTypeId.Set(1);
    NextRelationshipId.Set(1);
    NextChannelId = 0;
    CurrentSchemaVersion = 1;
    
    return true;
}

void FMaterialRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopedSpinLock Lock(RegistryLock);
        
        // Clear all registered items
        MaterialTypes.Empty();
        MaterialTypeNameMap.Empty();
        RelationshipMap.Empty();
        MaterialTypeHierarchy.Empty();
        MaterialPropertyMap.Empty();
        
        // Reset state
        bIsInitialized = false;
    }
}

bool FMaterialRegistry::IsInitialized() const
{
    return bIsInitialized;
}

FName FMaterialRegistry::GetRegistryName() const
{
    return FName(TEXT("MaterialRegistry"));
}

uint32 FMaterialRegistry::GetSchemaVersion() const
{
    return CurrentSchemaVersion;
}

bool FMaterialRegistry::Validate(TArray<FString>& OutErrors) const
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Material Registry is not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    bool bIsValid = true;
    
    // Validate material type map integrity
    for (const auto& TypeNamePair : MaterialTypeNameMap)
    {
        const FName& TypeName = TypeNamePair.Key;
        const uint32 TypeId = TypeNamePair.Value;
        
        if (!MaterialTypes.Contains(TypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Material type name '%s' references non-existent type ID %u"), *TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        else if (MaterialTypes[TypeId]->TypeName != TypeName)
        {
            OutErrors.Add(FString::Printf(TEXT("Material type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *TypeName.ToString(), TypeId, *MaterialTypes[TypeId]->TypeName.ToString()));
            bIsValid = false;
        }
    }
    
    // Validate parent-child relationships
    for (const auto& TypeInfoPair : MaterialTypes)
    {
        const uint32 TypeId = TypeInfoPair.Key;
        const TSharedRef<FMaterialTypeInfo>& TypeInfo = TypeInfoPair.Value;
        
        // Check if parent type exists (if specified)
        if (TypeInfo->ParentTypeId != 0)
        {
            if (!MaterialTypes.Contains(TypeInfo->ParentTypeId))
            {
                OutErrors.Add(FString::Printf(TEXT("Material type '%s' (ID %u) references non-existent parent type ID %u"),
                    *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->ParentTypeId));
                bIsValid = false;
            }
        }
        
        // Check channel ID validity if assigned
        if (TypeInfo->ChannelId >= 0)
        {
            // Check for duplicate channel IDs
            for (const auto& OtherTypePair : MaterialTypes)
            {
                if (OtherTypePair.Key != TypeId && OtherTypePair.Value->ChannelId == TypeInfo->ChannelId)
                {
                    OutErrors.Add(FString::Printf(TEXT("Material types '%s' and '%s' have duplicate channel ID %d"),
                        *TypeInfo->TypeName.ToString(), *OtherTypePair.Value->TypeName.ToString(), TypeInfo->ChannelId));
                    bIsValid = false;
                    break;
                }
            }
        }
    }
    
    // Validate material relationship integrity
    for (const auto& RelationshipPair : RelationshipMap)
    {
        const uint32 RelationshipId = RelationshipPair.Key;
        const TSharedRef<FMaterialRelationship>& Relationship = RelationshipPair.Value;
        
        // Verify relationship ID consistency
        if (Relationship->RelationshipId != RelationshipId)
        {
            OutErrors.Add(FString::Printf(TEXT("Material relationship ID mismatch: Relationship claims ID %u but is stored under ID %u"),
                Relationship->RelationshipId, RelationshipId));
            bIsValid = false;
        }
        
        // Verify source and target material types exist
        if (!MaterialTypes.Contains(Relationship->SourceTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Material relationship (ID %u) references non-existent source type ID %u"),
                RelationshipId, Relationship->SourceTypeId));
            bIsValid = false;
        }
        
        if (!MaterialTypes.Contains(Relationship->TargetTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Material relationship (ID %u) references non-existent target type ID %u"),
                RelationshipId, Relationship->TargetTypeId));
            bIsValid = false;
        }
        
        // Verify compatibility score is in valid range
        if (Relationship->CompatibilityScore < 0.0f || Relationship->CompatibilityScore > 1.0f)
        {
            OutErrors.Add(FString::Printf(TEXT("Material relationship (ID %u) has invalid compatibility score %.3f (must be between 0 and 1)"),
                RelationshipId, Relationship->CompatibilityScore));
            bIsValid = false;
        }
    }
    
    // Verify relationship lookup maps
    for (auto It = MaterialTypeHierarchy.CreateConstIterator(); It; ++It)
    {
        const uint32 SourceTypeId = It.Key();
        const uint32 RelationshipId = It.Value();
        
        // Verify source type exists
        if (!MaterialTypes.Contains(SourceTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy contains non-existent source type ID %u"),
                SourceTypeId));
            bIsValid = false;
        }
        
        // Verify relationship exists
        if (!RelationshipMap.Contains(RelationshipId))
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy references non-existent relationship ID %u"),
                RelationshipId));
            bIsValid = false;
        }
        else if (RelationshipMap[RelationshipId]->SourceTypeId != SourceTypeId)
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy inconsistency: relationship %u source is %u, not %u"),
                RelationshipId, RelationshipMap[RelationshipId]->SourceTypeId, SourceTypeId));
            bIsValid = false;
        }
    }
    
    // Similar verification for target map
    for (auto It = MaterialTypeHierarchy.CreateConstIterator(); It; ++It)
    {
        const uint32 TargetTypeId = It.Key();
        const uint32 RelationshipId = It.Value();
        
        // Verify target type exists
        if (!MaterialTypes.Contains(TargetTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy contains non-existent target type ID %u"),
                TargetTypeId));
            bIsValid = false;
        }
        
        // Verify relationship exists
        if (!RelationshipMap.Contains(RelationshipId))
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy references non-existent relationship ID %u"),
                RelationshipId));
            bIsValid = false;
        }
        else if (RelationshipMap[RelationshipId]->TargetTypeId != TargetTypeId)
        {
            OutErrors.Add(FString::Printf(TEXT("MaterialTypeHierarchy inconsistency: relationship %u target is %u, not %u"),
                RelationshipId, RelationshipMap[RelationshipId]->TargetTypeId, TargetTypeId));
            bIsValid = false;
        }
    }
    
    return bIsValid;
}

void FMaterialRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety
        FScopedSpinLock Lock(RegistryLock);
        
        // Clear all registered items
        MaterialTypes.Empty();
        MaterialTypeNameMap.Empty();
        RelationshipMap.Empty();
        MaterialTypeHierarchy.Empty();
        MaterialPropertyMap.Empty();
        
        // Reset counters using thread-safe methods
        NextTypeId.Set(1);
        NextRelationshipId.Set(1);
        NextChannelId = 0;
    }
}

bool FMaterialRegistry::SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - registry not initialized"));
        return false;
    }
    
    // Check if type exists
    if (!MaterialTypes.Contains(TypeId))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - type ID %u not found"), TypeId);
        return false;
    }
    
    // Get mutable type info
    TSharedRef<FMaterialTypeInfo>& TypeInfo = MaterialTypes[TypeId];
    
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
    
    // Integrate with MemoryPoolManager to update memory state if channel memory is allocated
    if (TypeInfo->ChannelId >= 0)
    {
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
        MigrationInfo.DataSize = 0; // Material types don't have fixed data size
        MigrationInfo.AlignmentRequirement = 16;
        
        // Find NarrowBandAllocator service
        FName PoolName = FName("HighPrecisionNBPool");
        IPoolAllocator* NBAllocator = MemoryManager->GetPool(PoolName);
        
        if (!NBAllocator)
        {
            // Try medium precision pool if high precision is not available
            PoolName = FName("MediumPrecisionNBPool");
            NBAllocator = MemoryManager->GetPool(PoolName);
        }
        
        if (NBAllocator)
        {
            // Perform migration through the pool
            bool bMigrationSuccess = NBAllocator->UpdateTypeVersion(MigrationInfo);
            
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
            UE_LOG(LogTemp, Warning, TEXT("Memory migration skipped for type '%s' - NarrowBandAllocator not found"),
                *TypeInfo->TypeName.ToString());
        }
    }
    
    return true; // Version was updated, even if no migration was performed
}

uint32 FMaterialRegistry::GetTypeVersion(uint32 TypeId) const
{
    // Check if type exists
    if (!MaterialTypes.Contains(TypeId))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetTypeVersion - type ID %u not found"), TypeId);
        return 0;
    }
    
    // Get type info
    const TSharedRef<FMaterialTypeInfo>& TypeInfo = MaterialTypes[TypeId];
    
    return TypeInfo->SchemaVersion;
}

uint32 FMaterialRegistry::RegisterMaterialType(
    const FMaterialTypeInfo& InTypeInfo,
    const FName& InTypeName,
    EMaterialPriority InPriority)
{
    // Make sure we're initialized
    if (!IsInitialized())
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Cannot register material type '%s': Registry not initialized"), *InTypeName.ToString());
        return 0;
    }
    
    // Ensure thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the type name is already registered
    if (MaterialTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Material type '%s' is already registered"), *InTypeName.ToString());
        return MaterialTypeNameMap[InTypeName];
    }
    
    // Generate a new unique ID for this type
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create type info
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe> TypeInfo = MakeShared<FMaterialTypeInfo, ESPMode::ThreadSafe>(InTypeInfo);
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->Priority = InPriority;
    
    // Add the type to our maps
    MaterialTypes.Add(TypeId, TypeInfo);
    MaterialTypeMap.Add(TypeId, TypeInfo);
    MaterialTypeNameMap.Add(InTypeName, TypeId);
    
    // Allocate memory for this type
    AllocateChannelMemory(TypeInfo);
    
    return TypeId;
}

uint32 FMaterialRegistry::RegisterMaterialRelationship(
    const FName& InSourceTypeName,
    const FName& InTargetTypeName,
    float InCompatibilityScore,
    bool bInCanBlend)
{
    // Make sure we're initialized
    if (!IsInitialized())
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Cannot register material relationship: Registry not initialized"));
        return 0;
    }
    
    // Ensure thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Find the type IDs
    uint32* SourceIdPtr = MaterialTypeNameMap.Find(InSourceTypeName);
    uint32* TargetIdPtr = MaterialTypeNameMap.Find(InTargetTypeName);
    
    // Validate types exist
    if (!SourceIdPtr)
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Cannot register material relationship: Source type '%s' not found"), *InSourceTypeName.ToString());
        return 0;
    }
    
    if (!TargetIdPtr)
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Cannot register material relationship: Target type '%s' not found"), *InTargetTypeName.ToString());
        return 0;
    }
    
    // Check if relationship already exists
    TArray<uint32> ExistingRelationships;
    if (MaterialTypeHierarchy.Contains(*SourceIdPtr))
    {
        MaterialTypeHierarchy.MultiFind(*SourceIdPtr, ExistingRelationships);
        
        for (uint32 RelationshipId : ExistingRelationships)
        {
            const FMaterialRelationship& Relationship = RelationshipMap[RelationshipId].Get();
            if (Relationship.TargetTypeId == *TargetIdPtr)
            {
                UE_LOG(LogMaterialRegistry, Warning, TEXT("Material relationship from '%s' to '%s' already exists (ID: %u)"),
                    *InSourceTypeName.ToString(), *InTargetTypeName.ToString(), RelationshipId);
                return RelationshipId;
            }
        }
    }
    
    // Create a new relationship ID
    uint32 RelationshipId = GenerateUniqueRelationshipId();
    
    // Create the relationship info
    TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe> RelationshipInfo = MakeShared<FMaterialRelationship, ESPMode::ThreadSafe>();
    RelationshipInfo->RelationshipId = RelationshipId;
    RelationshipInfo->SourceTypeId = *SourceIdPtr;
    RelationshipInfo->TargetTypeId = *TargetIdPtr;
    RelationshipInfo->SourceTypeName = InSourceTypeName;
    RelationshipInfo->TargetTypeName = InTargetTypeName;
    RelationshipInfo->CompatibilityScore = FMath::Clamp(InCompatibilityScore, 0.0f, 1.0f);
    RelationshipInfo->bCanBlend = bInCanBlend;
    
    // Register the relationship
    RelationshipMap.Add(RelationshipId, RelationshipInfo);
    
    // Add to source map
    if (!RelationshipsBySourceMap.Contains(*SourceIdPtr))
    {
        TArray<uint32> TargetTypes;
        TargetTypes.Add(RelationshipId);
        RelationshipsBySourceMap.Add(*SourceIdPtr, TargetTypes);
    }
    else
    {
        RelationshipsBySourceMap[*SourceIdPtr].Add(RelationshipId);
    }
    
    // Add to target map
    if (!RelationshipsByTargetMap.Contains(*TargetIdPtr))
    {
        TArray<uint32> SourceTypes;
        SourceTypes.Add(RelationshipId);
        RelationshipsByTargetMap.Add(*TargetIdPtr, SourceTypes);
    }
    else
    {
        RelationshipsByTargetMap[*TargetIdPtr].Add(RelationshipId);
    }
    
    return RelationshipId;
}

int32 FMaterialRegistry::AllocateMaterialChannel(uint32 InTypeId)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::AllocateMaterialChannel failed - registry not initialized"));
        return -1;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::AllocateMaterialChannel failed - type ID %u not found"), InTypeId);
        return -1;
    }
    
    // Check if a channel is already allocated for this material
    if (TypeInfoPtr->Get().ChannelId >= 0)
    {
        return TypeInfoPtr->Get().ChannelId;
    }
    
    // Allocate a new channel ID using thread-safe increment
    int32 ChannelId = NextChannelId++;
    
    // Update the material type info
    TypeInfoPtr->Get().ChannelId = ChannelId;
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::AllocateMaterialChannel - allocated channel %d for material '%s'"),
        ChannelId, *TypeInfoPtr->Get().TypeName.ToString());
    
    return ChannelId;
}

const FMaterialTypeInfo* FMaterialRegistry::GetMaterialTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type by ID
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        return &(TypeInfoPtr->Get());
    }
    
    return nullptr;
}

const FMaterialTypeInfo* FMaterialRegistry::GetMaterialTypeInfoByName(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = MaterialTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(*TypeIdPtr);
        if (TypeInfoPtr)
        {
            return &(TypeInfoPtr->Get());
        }
    }
    
    return nullptr;
}

const FMaterialRelationship* FMaterialRegistry::GetMaterialRelationship(uint32 InRelationshipId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up relationship by ID
    const TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe>* RelationshipPtr = RelationshipMap.Find(InRelationshipId);
    if (RelationshipPtr)
    {
        return &(RelationshipPtr->Get());
    }
    
    return nullptr;
}

TArray<FMaterialTypeInfo> FMaterialRegistry::GetAllMaterialTypes() const
{
    TArray<FMaterialTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Collect all material type infos
    Result.Reserve(MaterialTypes.Num());
    for (const auto& TypeInfoPair : MaterialTypes)
    {
        Result.Add(TypeInfoPair.Value.Get());
    }
    
    return Result;
}

TArray<FMaterialTypeInfo> FMaterialRegistry::GetDerivedMaterialTypes(uint32 InParentTypeId) const
{
    TArray<FMaterialTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the parent type is registered
    if (!MaterialTypes.Contains(InParentTypeId))
    {
        return Result;
    }
    
    // Collect all material types that have this parent
    for (const auto& TypeInfoPair : MaterialTypes)
    {
        if (TypeInfoPair.Value->ParentTypeId == InParentTypeId)
        {
            Result.Add(TypeInfoPair.Value.Get());
        }
    }
    
    return Result;
}

TArray<FMaterialRelationship> FMaterialRegistry::GetMaterialRelationships(uint32 InTypeId) const
{
    TArray<FMaterialRelationship> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Collect all relationships where this type is the source
    TArray<uint32> RelationshipIds;
    // Use TMultiMap's member MultiFind method instead of the global helper function
    TypeRelationships.MultiFind(InTypeId, RelationshipIds);
    
    for (uint32 RelationshipId : RelationshipIds)
    {
        const TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe>* RelationshipPtr = RelationshipMap.Find(RelationshipId);
        if (RelationshipPtr)
        {
            Result.Add(RelationshipPtr->Get());
        }
    }
    
    return Result;
}

bool FMaterialRegistry::IsMaterialTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    return MaterialTypeMap.Contains(InTypeId);
}

bool FMaterialRegistry::IsMaterialTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    return MaterialTypeNameMap.Contains(InTypeName);
}

bool FMaterialRegistry::IsMaterialDerivedFrom(uint32 InDerivedTypeId, uint32 InBaseTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if both types are registered
    if (!MaterialTypeMap.Contains(InDerivedTypeId) || !MaterialTypeMap.Contains(InBaseTypeId))
    {
        return false;
    }
    
    // Special case: a type is considered derived from itself
    if (InDerivedTypeId == InBaseTypeId)
    {
        return true;
    }
    
    // Walk the inheritance chain
    uint32 CurrentTypeId = InDerivedTypeId;
    while (CurrentTypeId != 0)
    {
        const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(CurrentTypeId);
        if (!TypeInfoPtr)
        {
            break;
        }
        
        CurrentTypeId = TypeInfoPtr->Get().ParentTypeId;
        
        // Check if we've found the base type
        if (CurrentTypeId == InBaseTypeId)
        {
            return true;
        }
    }
    
    return false;
}

bool FMaterialRegistry::UpdateMaterialProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::UpdateMaterialProperty failed - registry not initialized"));
        return false;
    }
    
    if (InPropertyName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::UpdateMaterialProperty failed - invalid property name"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::UpdateMaterialProperty failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Update the property based on its name
    if (InPropertyName == TEXT("ResourceValueMultiplier"))
    {
        TypeInfoPtr->Get().ResourceValueMultiplier = FCString::Atof(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("BaseMiningResistance"))
    {
        TypeInfoPtr->Get().BaseMiningResistance = FCString::Atof(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("SoundAmplificationFactor"))
    {
        TypeInfoPtr->Get().SoundAmplificationFactor = FCString::Atof(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("ParticleEmissionMultiplier"))
    {
        TypeInfoPtr->Get().ParticleEmissionMultiplier = FCString::Atof(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("IsMineable"))
    {
        TypeInfoPtr->Get().bIsMineable = InValue.ToBool();
        return true;
    }
    else if (InPropertyName == TEXT("IsResource"))
    {
        TypeInfoPtr->Get().bIsResource = InValue.ToBool();
        return true;
    }
    else if (InPropertyName == TEXT("CanFracture"))
    {
        TypeInfoPtr->Get().bCanFracture = InValue.ToBool();
        return true;
    }
    else if (InPropertyName == TEXT("VisualizationMaterial"))
    {
        TypeInfoPtr->Get().VisualizationMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(InValue));
        return true;
    }
    else if (InPropertyName == TEXT("MiningSound"))
    {
        TypeInfoPtr->Get().MiningSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(InValue));
        return true;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("FMaterialRegistry::UpdateMaterialProperty - unknown property '%s'"), *InPropertyName.ToString());
    return false;
}

bool FMaterialRegistry::RegisterMaterialProperty(uint32 InTypeId, TSharedPtr<FMaterialPropertyBase> InProperty)
{
    if (!IsInitialized() || !InProperty.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialProperty failed - invalid state or property"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    if (!MaterialTypeMap.Contains(InTypeId))
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialProperty failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Ensure property map exists for this type
    EnsurePropertyMap(InTypeId);
    
    // Get the property name
    const FName PropertyName = InProperty->PropertyName;
    if (PropertyName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialProperty failed - property has no name"));
        return false;
    }
    
    // Add or update the property
    if (!MaterialPropertyMap.Contains(InTypeId))
    {
        TMap<FName, TSharedPtr<FMaterialPropertyBase>> PropertyMap;
        PropertyMap.Add(PropertyName, InProperty);
        MaterialPropertyMap.Add(InTypeId, PropertyMap);
    }
    else
    {
        MaterialPropertyMap[InTypeId].Add(PropertyName, InProperty);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::RegisterMaterialProperty - registered property '%s' for type ID %u"),
        *PropertyName.ToString(), InTypeId);
    
    return true;
}

TSharedPtr<FMaterialPropertyBase> FMaterialRegistry::GetMaterialProperty(uint32 InTypeId, const FName& InPropertyName) const
{
    if (!IsInitialized() || InPropertyName.IsNone())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    if (!MaterialTypeMap.Contains(InTypeId))
    {
        return nullptr;
    }
    
    // Check if any properties are registered for this type
    const TMap<FName, TSharedPtr<FMaterialPropertyBase>>* PropertiesPtr = nullptr;
    {
        const auto* FoundMap = MaterialPropertyMap.Find(InTypeId);
        if (FoundMap != nullptr)
        {
            PropertiesPtr = FoundMap;
        }
    }
    
    if (!PropertiesPtr)
    {
        // If not found in this type, check if it has a parent
        const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
        if (TypeInfoPtr && TypeInfoPtr->Get().ParentTypeId != 0)
        {
            // Recurse to check parent type
            return GetMaterialProperty(TypeInfoPtr->Get().ParentTypeId, InPropertyName);
        }
        
        return nullptr;
    }
    
    // Look up property by name
    TSharedPtr<FMaterialPropertyBase> PropertyPtr = PropertiesPtr->FindRef(InPropertyName);
    if (PropertyPtr.IsValid())
    {
        return PropertyPtr;
    }
    
    // If not found in this type, check if it has a parent
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (TypeInfoPtr && TypeInfoPtr->Get().ParentTypeId != 0 && TypeInfoPtr->Get().ParentTypeId != InTypeId)
    {
        // Recurse to check parent type
        return GetMaterialProperty(TypeInfoPtr->Get().ParentTypeId, InPropertyName);
    }
    
    return nullptr;
}

TMap<FName, TSharedPtr<FMaterialPropertyBase>> FMaterialRegistry::GetAllMaterialProperties(uint32 InTypeId) const
{
    TMap<FName, TSharedPtr<FMaterialPropertyBase>> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    if (!MaterialTypeMap.Contains(InTypeId))
    {
        return Result;
    }
    
    // Get material type info to check for parent
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        return Result;
    }
    
    // If this type has a parent, get its properties first (inheritance)
    if (TypeInfoPtr->Get().ParentTypeId != 0 && TypeInfoPtr->Get().ParentTypeId != InTypeId)
    {
        Result = GetAllMaterialProperties(TypeInfoPtr->Get().ParentTypeId);
    }
    
    // Get properties for this type
    const TMap<FName, TSharedPtr<FMaterialPropertyBase>>* TypePropertiesPtr = MaterialPropertyMap.Find(InTypeId);
    if (TypePropertiesPtr)
    {
        // Add or override properties from this type
        for (const auto& PropertyPair : *TypePropertiesPtr)
        {
            if (PropertyPair.Value.IsValid())
            {
                Result.Add(PropertyPair.Key, PropertyPair.Value);
            }
        }
    }
    
    return Result;
}

void FMaterialRegistry::EnsurePropertyMap(uint32 InTypeId)
{
    // This function is called within a locked context, so it's thread-safe
    if (!MaterialPropertyMap.Contains(InTypeId))
    {
        MaterialPropertyMap.Add(InTypeId, TMap<FName, TSharedPtr<FMaterialPropertyBase>>());
    }
}

TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* FMaterialRegistry::GetMutableMaterialTypeInfo(uint32 InTypeId)
{
    // This function is called within a locked context, so it's thread-safe
    return MaterialTypeMap.Find(InTypeId);
}

uint32 FMaterialRegistry::GenerateUniqueTypeId()
{
    // Use thread-safe increment
    return NextTypeId.Increment();
}

uint32 FMaterialRegistry::GenerateUniqueRelationshipId()
{
    // Use thread-safe increment
    return NextRelationshipId.Increment();
}

bool FMaterialRegistry::InheritPropertiesFromParent(uint32 InChildTypeId, uint32 InParentTypeId, bool bOverrideExisting)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::InheritPropertiesFromParent failed - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if both types are registered
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* ChildTypeInfoPtr = MaterialTypeMap.Find(InChildTypeId);
    if (!ChildTypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::InheritPropertiesFromParent failed - child type ID %u not found"), InChildTypeId);
        return false;
    }
    
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* ParentTypeInfoPtr = MaterialTypeMap.Find(InParentTypeId);
    if (!ParentTypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::InheritPropertiesFromParent failed - parent type ID %u not found"), InParentTypeId);
        return false;
    }
    
    // Get parent properties
    const TMap<FName, TSharedPtr<FMaterialPropertyBase>>* ParentPropertiesPtr = MaterialPropertyMap.Find(InParentTypeId);
    if (!ParentPropertiesPtr || ParentPropertiesPtr->Num() == 0)
    {
        // No properties to inherit
        return true;
    }
    
    // Ensure property map exists for child
    EnsurePropertyMap(InChildTypeId);
    
    // Copy inheritable properties from parent to child
    int32 InheritedCount = 0;
    for (const auto& ParentPropertyPair : *ParentPropertiesPtr)
    {
        const FName& PropertyName = ParentPropertyPair.Key;
        const TSharedPtr<FMaterialPropertyBase>& ParentProperty = ParentPropertyPair.Value;
        
        if (!ParentProperty.IsValid() || !ParentProperty->bInheritable)
        {
            continue;
        }
        
        // Check if child already has this property
        TMap<FName, TSharedPtr<FMaterialPropertyBase>>& ChildProperties = MaterialPropertyMap[InChildTypeId];
        const TSharedPtr<FMaterialPropertyBase>* ExistingPropertyPtr = ChildProperties.Find(PropertyName);
        
        if (!ExistingPropertyPtr || !ExistingPropertyPtr->IsValid() || bOverrideExisting)
        {
            // Clone property and add to child
            TSharedPtr<FMaterialPropertyBase> ClonedProperty = ParentProperty->Clone();
            if (ClonedProperty.IsValid())
            {
                ChildProperties.Add(PropertyName, ClonedProperty);
                InheritedCount++;
            }
        }
    }
    
    // Update parent-child relationship
    ChildTypeInfoPtr->Get().ParentTypeId = InParentTypeId;
    
    // Inherit some basic properties from parent type
    const FMaterialTypeInfo& ParentInfo = ParentTypeInfoPtr->Get();
    FMaterialTypeInfo& ChildInfo = ChildTypeInfoPtr->Get();
    
    // Only override these if they're at default values
    if (ChildInfo.BaseMiningResistance == 1.0f)
        ChildInfo.BaseMiningResistance = ParentInfo.BaseMiningResistance;
        
    if (ChildInfo.ResourceValueMultiplier == 1.0f)
        ChildInfo.ResourceValueMultiplier = ParentInfo.ResourceValueMultiplier;
        
    if (ChildInfo.SoundAmplificationFactor == 1.0f)
        ChildInfo.SoundAmplificationFactor = ParentInfo.SoundAmplificationFactor;
        
    if (ChildInfo.ParticleEmissionMultiplier == 1.0f)
        ChildInfo.ParticleEmissionMultiplier = ParentInfo.ParticleEmissionMultiplier;
    
    // Inherit capabilities
    ChildInfo.Capabilities |= ParentInfo.Capabilities;
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::InheritPropertiesFromParent - inherited %d properties from '%s' to '%s'"),
        InheritedCount, *ParentInfo.TypeName.ToString(), *ChildInfo.TypeName.ToString());
    
    return true;
}

EMaterialCapabilities FMaterialRegistry::GetMaterialCapabilities(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return EMaterialCapabilities::None;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type by ID
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        return TypeInfoPtr->Get().Capabilities;
    }
    
    return EMaterialCapabilities::None;
}

bool FMaterialRegistry::AddMaterialCapability(uint32 InTypeId, EMaterialCapabilities InCapability)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type by ID
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        TypeInfoPtr->Get().AddCapability(InCapability);
        return true;
    }
    
    return false;
}

bool FMaterialRegistry::RemoveMaterialCapability(uint32 InTypeId, EMaterialCapabilities InCapability)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type by ID
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        TypeInfoPtr->Get().RemoveCapability(InCapability);
        return true;
    }
    
    return false;
}

uint32 FMaterialRegistry::CloneMaterialType(uint32 InSourceTypeId, const FName& InNewTypeName, bool bInheritRelationships)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::CloneMaterialType failed - registry not initialized"));
        return 0;
    }
    
    if (InNewTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::CloneMaterialType failed - invalid type name"));
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if source type is registered
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* SourceTypeInfoPtr = MaterialTypeMap.Find(InSourceTypeId);
    if (!SourceTypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::CloneMaterialType failed - source type ID %u not found"), InSourceTypeId);
        return 0;
    }
    
    // Check if target name is already in use
    if (MaterialTypeNameMap.Contains(InNewTypeName))
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::CloneMaterialType failed - target name '%s' already in use"), *InNewTypeName.ToString());
        return 0;
    }
    
    // Create a deep copy of the source type info
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe> NewTypeInfo = MakeShared<FMaterialTypeInfo, ESPMode::ThreadSafe>();
    
    // Copy all properties from source
    const FMaterialTypeInfo& SourceInfo = SourceTypeInfoPtr->Get();
    FMaterialTypeInfo& NewInfo = NewTypeInfo.Get();
    
    // Generate a new unique ID
    uint32 NewTypeId = GenerateUniqueTypeId();
    
    // Copy properties, but update the IDs and name
    NewInfo = SourceInfo;
    NewInfo.TypeId = NewTypeId;
    NewInfo.TypeName = InNewTypeName;
    NewInfo.HotReloadId = FGuid::NewGuid(); // Generate a new hot reload ID
    
    // Register the new type
    MaterialTypeMap.Add(NewTypeId, NewTypeInfo);
    MaterialTypeNameMap.Add(InNewTypeName, NewTypeId);
    
    // Copy custom properties
    const TMap<FName, TSharedPtr<FMaterialPropertyBase>>* SourcePropertiesPtr = nullptr;
    {
        const auto* FoundMap = MaterialPropertyMap.Find(InSourceTypeId);
        if (FoundMap != nullptr)
        {
            SourcePropertiesPtr = FoundMap;
        }
    }
    
    if (SourcePropertiesPtr && SourcePropertiesPtr->Num() > 0)
    {
        // Create a new property map for the cloned type
        TMap<FName, TSharedPtr<FMaterialPropertyBase>> NewProperties;
        
        // Clone each property
        for (const auto& PropertyPair : *SourcePropertiesPtr)
        {
            if (PropertyPair.Value.IsValid())
            {
                TSharedPtr<FMaterialPropertyBase> ClonedProperty = PropertyPair.Value->Clone();
                if (ClonedProperty.IsValid())
                {
                    NewProperties.Add(PropertyPair.Key, ClonedProperty);
                }
            }
        }
        
        // Add the properties to the registry
        MaterialPropertyMap.Add(NewTypeId, NewProperties);
    }
    
    // Copy relationships if requested
    if (bInheritRelationships)
    {
        // Copy outgoing relationships (where source type is the source)
        TArray<uint32> SourceRelationshipIds;
        ::MultiFind(RelationshipsBySourceMap, InSourceTypeId, SourceRelationshipIds);
        
        for (uint32 RelationshipId : SourceRelationshipIds)
        {
            const TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe>* RelationshipPtr = RelationshipMap.Find(RelationshipId);
            if (RelationshipPtr)
            {
                // Create a new relationship with the cloned type as source
                uint32 NewRelationshipId = GenerateUniqueRelationshipId();
                TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe> NewRelationship = MakeShared<FMaterialRelationship, ESPMode::ThreadSafe>();
                
                // Copy relationship properties
                NewRelationship->RelationshipId = NewRelationshipId;
                NewRelationship->SourceTypeId = NewTypeId; // Use cloned type as source
                NewRelationship->TargetTypeId = RelationshipPtr->Get().TargetTypeId;
                NewRelationship->CompatibilityScore = RelationshipPtr->Get().CompatibilityScore;
                NewRelationship->bCanBlend = RelationshipPtr->Get().bCanBlend;
                NewRelationship->BlendSharpness = RelationshipPtr->Get().BlendSharpness;
                NewRelationship->InteractionType = RelationshipPtr->Get().InteractionType;
                NewRelationship->TransitionEffect = RelationshipPtr->Get().TransitionEffect;
                NewRelationship->InteractionPriority = RelationshipPtr->Get().InteractionPriority;
                NewRelationship->SchemaVersion = RelationshipPtr->Get().SchemaVersion;
                
                // Register the new relationship
                RelationshipMap.Add(NewRelationshipId, NewRelationship);
                
                // Add to source map (with proper array handling)
                if (!RelationshipsBySourceMap.Contains(NewTypeId))
                {
                    TArray<uint32> Relationships;
                    Relationships.Add(NewRelationshipId);
                    RelationshipsBySourceMap.Add(NewTypeId, Relationships);
                }
                else
                {
                    RelationshipsBySourceMap[NewTypeId].Add(NewRelationshipId);
                }
                
                // Add to target map (with proper array handling)
                if (!RelationshipsByTargetMap.Contains(NewRelationship->TargetTypeId))
                {
                    TArray<uint32> Relationships;
                    Relationships.Add(NewRelationshipId);
                    RelationshipsByTargetMap.Add(NewRelationship->TargetTypeId, Relationships);
                }
                else
                {
                    RelationshipsByTargetMap[NewRelationship->TargetTypeId].Add(NewRelationshipId);
                }
            }
        }
        
        // Copy incoming relationships (where source type is the target)
        TArray<uint32> TargetRelationshipIds;
        ::MultiFind(RelationshipsByTargetMap, InSourceTypeId, TargetRelationshipIds);
        
        for (uint32 RelationshipId : TargetRelationshipIds)
        {
            const TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe>* RelationshipPtr = RelationshipMap.Find(RelationshipId);
            if (RelationshipPtr)
            {
                // Create a new relationship with the cloned type as target
                uint32 NewRelationshipId = GenerateUniqueRelationshipId();
                TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe> NewRelationship = MakeShared<FMaterialRelationship, ESPMode::ThreadSafe>();
                
                // Copy relationship properties
                NewRelationship->RelationshipId = NewRelationshipId;
                NewRelationship->SourceTypeId = RelationshipPtr->Get().SourceTypeId;
                NewRelationship->TargetTypeId = NewTypeId; // Use cloned type as target
                NewRelationship->CompatibilityScore = RelationshipPtr->Get().CompatibilityScore;
                NewRelationship->bCanBlend = RelationshipPtr->Get().bCanBlend;
                NewRelationship->BlendSharpness = RelationshipPtr->Get().BlendSharpness;
                NewRelationship->InteractionType = RelationshipPtr->Get().InteractionType;
                NewRelationship->TransitionEffect = RelationshipPtr->Get().TransitionEffect;
                NewRelationship->InteractionPriority = RelationshipPtr->Get().InteractionPriority;
                NewRelationship->SchemaVersion = RelationshipPtr->Get().SchemaVersion;
                
                // Register the new relationship
                RelationshipMap.Add(NewRelationshipId, NewRelationship);
                
                // Add to source map
                if (!RelationshipsBySourceMap.Contains(NewRelationship->SourceTypeId))
                {
                    TArray<uint32> Relationships;
                    Relationships.Add(NewRelationshipId);
                    RelationshipsBySourceMap.Add(NewRelationship->SourceTypeId, Relationships);
                }
                else
                {
                    RelationshipsBySourceMap[NewRelationship->SourceTypeId].Add(NewRelationshipId);
                }
                
                // Add to target map
                if (!RelationshipsByTargetMap.Contains(NewTypeId))
                {
                    TArray<uint32> Relationships;
                    Relationships.Add(NewRelationshipId);
                    RelationshipsByTargetMap.Add(NewTypeId, Relationships);
                }
                else
                {
                    RelationshipsByTargetMap[NewTypeId].Add(NewRelationshipId);
                }
            }
        }
    }
    
    return NewTypeId;
}

bool FMaterialRegistry::MigrateAllTypes()
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::MigrateAllTypes failed - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    bool bAllMigrationsSuccessful = true;
    
    // Migrate all material types
    for (auto& TypeInfoPair : MaterialTypeMap)
    {
        FMaterialTypeInfo& TypeInfo = TypeInfoPair.Value.Get();
        
        if (TypeInfo.SchemaVersion < CurrentSchemaVersion)
        {
            // Migrate to current version
            if (TypeInfo.MigrateToCurrentVersion(CurrentSchemaVersion))
            {
                UE_LOG(LogTemp, Log, TEXT("FMaterialRegistry::MigrateAllTypes - migrated type '%s' from schema %u to %u"),
                    *TypeInfo.TypeName.ToString(), TypeInfo.SchemaVersion, CurrentSchemaVersion);
                TypeInfo.SchemaVersion = CurrentSchemaVersion;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::MigrateAllTypes - failed to migrate type '%s' from schema %u to %u"),
                    *TypeInfo.TypeName.ToString(), TypeInfo.SchemaVersion, CurrentSchemaVersion);
                bAllMigrationsSuccessful = false;
            }
        }
    }
    
    // Migrate all relationships
    for (auto& RelationshipPair : RelationshipMap)
    {
        FMaterialRelationship& Relationship = RelationshipPair.Value.Get();
        
        if (Relationship.SchemaVersion < CurrentSchemaVersion)
        {
            // Migrate to current version
            if (Relationship.MigrateToCurrentVersion(CurrentSchemaVersion))
            {
                UE_LOG(LogTemp, Log, TEXT("FMaterialRegistry::MigrateAllTypes - migrated relationship %u from schema %u to %u"),
                    Relationship.RelationshipId, Relationship.SchemaVersion, CurrentSchemaVersion);
                Relationship.SchemaVersion = CurrentSchemaVersion;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::MigrateAllTypes - failed to migrate relationship %u from schema %u to %u"),
                    Relationship.RelationshipId, Relationship.SchemaVersion, CurrentSchemaVersion);
                bAllMigrationsSuccessful = false;
            }
        }
    }
    
    return bAllMigrationsSuccessful;
}

void FMaterialRegistry::CreateBlueprintWrappers()
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::CreateBlueprintWrappers failed - registry not initialized"));
        return;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    UE_LOG(LogTemp, Log, TEXT("FMaterialRegistry::CreateBlueprintWrappers - creating blueprint wrappers for %d material types"),
        MaterialTypeMap.Num());
    
    // Create blueprint wrappers for all material types
    for (const auto& TypeInfoPair : MaterialTypeMap)
    {
        const FMaterialTypeInfo& TypeInfo = TypeInfoPair.Value.Get();
        
        // Create blueprint wrapper for this type
        TypeInfo.CreateBlueprintWrapper();
    }
}

EMaterialCapabilities FMaterialRegistry::DetectHardwareCapabilities()
{
    // Initialize with no capabilities
    EMaterialCapabilities DetectedCapabilities = EMaterialCapabilities::None;
    
    // Basic capabilities every platform supports
    DetectedCapabilities |= EMaterialCapabilities::SupportsBlending;
    DetectedCapabilities |= EMaterialCapabilities::SupportsProcGen;
    DetectedCapabilities |= EMaterialCapabilities::SupportsNoise;
    DetectedCapabilities |= EMaterialCapabilities::SupportsPatterns;
    
    // Check for SIMD capabilities
#if PLATFORM_ENABLE_VECTORINTRINSICS
    DetectedCapabilities |= EMaterialCapabilities::SupportsSSE;
#endif

    // Check for GPU compute support using compile-time flags
#if WITH_EDITOR || WITH_EDITORONLY_DATA
    DetectedCapabilities |= EMaterialCapabilities::SupportsGPUCompute;
#endif
    
    // Check for multi-threading support - always available in modern UE
    DetectedCapabilities |= EMaterialCapabilities::SupportsMultiThreading;

    // Store detected capabilities
    HardwareCapabilities = DetectedCapabilities;
    
    UE_LOG(LogTemp, Log, TEXT("FMaterialRegistry::DetectHardwareCapabilities - Detected hardware capabilities: 0x%08X"), 
        static_cast<uint32>(DetectedCapabilities));
    
    return DetectedCapabilities;
}

TArray<FMaterialTypeInfo> FMaterialRegistry::GetMaterialTypesByCategory(const FName& InCategory) const
{
    TArray<FMaterialTypeInfo> Result;
    
    if (!IsInitialized() || InCategory.IsNone())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Get all type IDs in the specified category
    TArray<uint32> CategoryTypeIds;
    MaterialTypesByCategoryMap.MultiFind(InCategory, CategoryTypeIds);
    
    // Get material type info for each ID
    for (uint32 TypeId : CategoryTypeIds)
    {
        const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(TypeId);
        if (TypeInfoPtr)
        {
            Result.Add(TypeInfoPtr->Get());
        }
    }
    
    return Result;
}

bool FMaterialRegistry::SetMaterialCategory(uint32 InTypeId, const FName& InCategory)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::SetMaterialCategory failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Remove from the old category
    const FName OldCategory = TypeInfoPtr->Get().Category;
    if (!OldCategory.IsNone())
    {
        // Find and remove the type ID from the old category
        TArray<uint32> OldCategoryTypeIds;
        MaterialTypesByCategoryMap.MultiFind(OldCategory, OldCategoryTypeIds);
        for (int32 i = 0; i < OldCategoryTypeIds.Num(); ++i)
        {
            if (OldCategoryTypeIds[i] == InTypeId)
            {
                MaterialTypesByCategoryMap.RemoveSingle(OldCategory, InTypeId);
                break;
            }
        }
    }
    
    // Update the type info with the new category
    TypeInfoPtr->Get().Category = InCategory;
    
    // Add to the new category if it's not none
    if (!InCategory.IsNone())
    {
        MaterialTypesByCategoryMap.Add(InCategory, InTypeId);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::SetMaterialCategory - set category for type '%s' from '%s' to '%s'"),
        *TypeInfoPtr->Get().TypeName.ToString(), *OldCategory.ToString(), *InCategory.ToString());
    
    return true;
}

bool FMaterialRegistry::SetupMaterialFields(uint32 InTypeId, bool bEnableVectorization)
{
    if (!IsInitialized())
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("FMaterialRegistry::SetupMaterialFields failed - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the material type is registered
    if (!MaterialTypeMap.Contains(InTypeId))
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("FMaterialRegistry::SetupMaterialFields failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Get material type info
    const TSharedRef<FMaterialTypeInfo>& TypeInfo = MaterialTypeMap[InTypeId];
    
    // Get memory management service from service locator
    IServiceLocator* ServiceLocator = &IServiceLocator::Get();
    if (!ServiceLocator)
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("FMaterialRegistry::SetupMaterialFields failed - ServiceLocator not available"));
        return false;
    }
    
    // Get narrow band allocator - use default zone and region IDs
    FNarrowBandAllocator* NarrowBandAllocator = ServiceLocator->ResolveService<FNarrowBandAllocator>(INDEX_NONE, INDEX_NONE);
    if (!NarrowBandAllocator)
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("FMaterialRegistry::SetupMaterialFields failed - NarrowBandAllocator not available"));
        return false;
    }
    
    // Determine appropriate SIMD instruction set based on material capabilities
    ESIMDInstructionSet SIMDType = ESIMDInstructionSet::None;
    uint32 FieldAlignment = 16; // Default to 16-byte alignment
    
    // Check supported SIMD capabilities
    if ((TypeInfo->Capabilities & EMaterialCapabilities::SupportsAVX2) != EMaterialCapabilities::None)
    {
        SIMDType = ESIMDInstructionSet::AVX2;
        FieldAlignment = 32;
    }
    else if ((TypeInfo->Capabilities & EMaterialCapabilities::SupportsAVX) != EMaterialCapabilities::None)
    {
        SIMDType = ESIMDInstructionSet::AVX;
        FieldAlignment = 32;
    }
    else if ((TypeInfo->Capabilities & EMaterialCapabilities::SupportsSSE) != EMaterialCapabilities::None)
    {
        SIMDType = ESIMDInstructionSet::SSE;
        FieldAlignment = 16;
    }
    else if ((TypeInfo->Capabilities & EMaterialCapabilities::SupportsNeon) != EMaterialCapabilities::None)
    {
        SIMDType = ESIMDInstructionSet::Neon;
        FieldAlignment = 16;
    }
    
    // Adjust alignment based on material type properties
    bool bIsFluid = false; // Default to false since this field doesn't exist in FMaterialTypeInfo
    bool bIsMultiLayered = false; // Default to false since this field doesn't exist in FMaterialTypeInfo
    
    // Note: We would check these properties properly if they existed in FMaterialTypeInfo
    // For now, we'll just use the default values
    
    if (bIsFluid)
    {
        // Fluids benefit from higher alignment for vector operations
        FieldAlignment = FMath::Max(FieldAlignment, 32u);
    }
    
    if (bIsMultiLayered)
    {
        // Multi-layered materials need stricter alignment for field interleaving
        FieldAlignment = FMath::Max(FieldAlignment, 16u);
    }
    
    // Configure SIMD layout in the narrow band allocator
    bool bResult = NarrowBandAllocator->ConfigureSIMDLayout(
        InTypeId,
        FieldAlignment,
        bEnableVectorization,
        SIMDType
    );
    
    if (bResult)
    {
        UE_LOG(LogMaterialRegistry, Log, TEXT("FMaterialRegistry::SetupMaterialFields - Configured SIMD layout for material '%s' (ID %u) with alignment %u bytes, SIMD type %d"),
            *TypeInfo->TypeName.ToString(), InTypeId, FieldAlignment, static_cast<int32>(SIMDType));
    }
    else
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("FMaterialRegistry::SetupMaterialFields - Failed to configure SIMD layout for material '%s' (ID %u)"),
            *TypeInfo->TypeName.ToString(), InTypeId);
    }
    
    return bResult;
}

bool FMaterialRegistry::CreateTypeHierarchyVisualization(FString& OutVisualizationData) const
{
    if (!IsInitialized())
    {
        OutVisualizationData = TEXT("Material Registry is not initialized");
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Start with an empty string
    OutVisualizationData.Empty();
    
    // Generate a header
    OutVisualizationData.Append(TEXT("Material Type Hierarchy Visualization\n"));
    OutVisualizationData.Append(TEXT("==================================\n\n"));
    
    // Find root types (those with no parent)
    TArray<uint32> RootTypeIds;
    for (const auto& TypeInfoPair : MaterialTypeMap)
    {
        if (TypeInfoPair.Value->ParentTypeId == 0)
        {
            RootTypeIds.Add(TypeInfoPair.Key);
        }
    }
    
    // Sort root types by name for consistent output
    RootTypeIds.Sort([this](uint32 A, uint32 B) {
        const FName& NameA = MaterialTypeMap[A]->TypeName;
        const FName& NameB = MaterialTypeMap[B]->TypeName;
        return NameA.ToString() < NameB.ToString();
    });
    
    // Recursively build the hierarchy for each root type
    for (uint32 RootTypeId : RootTypeIds)
    {
        VisualizeTypeHierarchy(RootTypeId, 0, OutVisualizationData);
    }
    
    // Add relationship information
    OutVisualizationData.Append(TEXT("\nMaterial Relationships\n"));
    OutVisualizationData.Append(TEXT("=====================\n\n"));
    
    // Group relationships by source type
    TMap<uint32, TArray<uint32>> RelationshipsBySource;
    for (const auto& RelationshipPair : RelationshipMap)
    {
        const uint32 SourceTypeId = RelationshipPair.Value->SourceTypeId;
        RelationshipsBySource.FindOrAdd(SourceTypeId).Add(RelationshipPair.Key);
    }
    
    // Sort source types by name for consistent output
    TArray<uint32> SourceTypeIds;
    RelationshipsBySource.GetKeys(SourceTypeIds);
    SourceTypeIds.Sort([this](uint32 A, uint32 B) {
        const FName& NameA = MaterialTypeMap[A]->TypeName;
        const FName& NameB = MaterialTypeMap[B]->TypeName;
        return NameA.ToString() < NameB.ToString();
    });
    
    // Print relationships for each source type
    for (uint32 SourceTypeId : SourceTypeIds)
    {
        const FName& SourceName = MaterialTypeMap[SourceTypeId]->TypeName;
        OutVisualizationData.Append(FString::Printf(TEXT("From: %s\n"), *SourceName.ToString()));
        
        const TArray<uint32>& RelationshipIds = RelationshipsBySource[SourceTypeId];
        for (uint32 RelationshipId : RelationshipIds)
        {
            const FMaterialRelationship& Relationship = RelationshipMap[RelationshipId].Get();
            const FName& TargetName = MaterialTypeMap[Relationship.TargetTypeId]->TypeName;
            
            OutVisualizationData.Append(FString::Printf(TEXT("  To: %s (Compatibility: %.2f, Can Blend: %s)\n"),
                *TargetName.ToString(),
                Relationship.CompatibilityScore,
                Relationship.bCanBlend ? TEXT("Yes") : TEXT("No")));
        }
        
        OutVisualizationData.Append(TEXT("\n"));
    }
    
    return true;
}

void FMaterialRegistry::VisualizeTypeHierarchy(uint32 InTypeId, int32 InDepth, FString& OutVisualizationData) const
{
    // This is a helper function called by CreateTypeHierarchyVisualization
    const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        return;
    }
    
    const FMaterialTypeInfo& TypeInfo = TypeInfoPtr->Get();
    
    // Create indentation based on depth
    FString Indent;
    for (int32 i = 0; i < InDepth; ++i)
    {
        Indent.Append(TEXT("  "));
    }
    
    // Add this type to the visualization
    OutVisualizationData.Append(FString::Printf(TEXT("%s%s (ID: %u, Priority: %s)\n"),
        *Indent,
        *TypeInfo.TypeName.ToString(),
        TypeInfo.TypeId,
        *StaticEnum<EMaterialPriority>()->GetNameStringByValue((int64)TypeInfo.Priority)));
    
    // Find all children of this type
    TArray<uint32> ChildTypeIds;
    for (const auto& TypeInfoPair : MaterialTypeMap)
    {
        if (TypeInfoPair.Value->ParentTypeId == InTypeId)
        {
            ChildTypeIds.Add(TypeInfoPair.Key);
        }
    }
    
    // Sort children by name for consistent output
    ChildTypeIds.Sort([this](uint32 A, uint32 B) {
        const FName& NameA = MaterialTypeMap[A]->TypeName;
        const FName& NameB = MaterialTypeMap[B]->TypeName;
        return NameA.ToString() < NameB.ToString();
    });
    
    // Recursively visualize all children
    for (uint32 ChildTypeId : ChildTypeIds)
    {
        VisualizeTypeHierarchy(ChildTypeId, InDepth + 1, OutVisualizationData);
    }
}

// Implementation of the CreateBlueprintWrapper method for Blueprint integration
void FMaterialTypeInfo::CreateBlueprintWrapper() const
{
#if WITH_EDITOR
    // In editor builds, we create a more detailed wrapper with editor metadata
    UE_LOG(LogTemp, Verbose, TEXT("Creating blueprint wrapper for material type '%s'"), *TypeName.ToString());
    
    // Additional editor-specific code could go here
#endif

    // Skip if visualization material is not set
    if (!VisualizationMaterial.IsValid())
    {
        return;
    }
    
    // Create a dynamic material instance if needed
    if (!CachedVisualizationInstance.IsValid())
    {
        UMaterialInterface* BaseMaterial = VisualizationMaterial.LoadSynchronous();
        if (BaseMaterial)
        {
            UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, nullptr);
            if (DynamicMaterial)
            {
                // Set common material parameters
                DynamicMaterial->SetScalarParameterValue(TEXT("BaseMiningResistance"), BaseMiningResistance);
                DynamicMaterial->SetScalarParameterValue(TEXT("ResourceValueMultiplier"), ResourceValueMultiplier);
                
                // Set material type ID as a parameter
                DynamicMaterial->SetScalarParameterValue(TEXT("MaterialTypeId"), static_cast<float>(TypeId));
                
                // Set capability flags
                uint32 CapabilityFlags = static_cast<uint32>(Capabilities);
                DynamicMaterial->SetScalarParameterValue(TEXT("MaterialCapabilities"), static_cast<float>(CapabilityFlags));
                
                // Set priority as an integer
                DynamicMaterial->SetScalarParameterValue(TEXT("MaterialPriority"), static_cast<float>(static_cast<uint8>(Priority)));
                
                // Store the created dynamic instance
                CachedVisualizationInstance = DynamicMaterial;
            }
        }
    }
    
    // After creating the blueprint wrapper, we update any existing instances
    if (CachedVisualizationInstance.IsValid())
    {
        UMaterialInstanceDynamic* DynamicMaterial = CachedVisualizationInstance.Get();
        
        // Update dynamic properties that might have changed
        DynamicMaterial->SetScalarParameterValue(TEXT("BaseMiningResistance"), BaseMiningResistance);
        DynamicMaterial->SetScalarParameterValue(TEXT("ResourceValueMultiplier"), ResourceValueMultiplier);
    }
}

// Implementation of the MigrateToCurrentVersion method for schema upgrades
bool FMaterialTypeInfo::MigrateToCurrentVersion(uint32 CurrentSchemaVersion)
{
    // Nothing to migrate if already at current version
    if (SchemaVersion >= CurrentSchemaVersion)
    {
        return true;
    }
    
    bool bSuccess = true;
    
    // Perform migrations based on the current schema version
    if (SchemaVersion < 2 && CurrentSchemaVersion >= 2)
    {
        // Migration from schema 1 to 2
        // In this example, schema 2 added support for CategoryName
        if (Category.IsNone())
        {
            // Set a default category based on material properties
            if (bIsResource)
            {
                Category = FName(TEXT("Resources"));
            }
            else
            {
                Category = FName(TEXT("Terrain"));
            }
        }
        
        // Update to schema 2
        SchemaVersion = 2;
    }
    
    // Additional migrations for future schema versions
    if (SchemaVersion < 3 && CurrentSchemaVersion >= 3)
    {
        // Migration from schema 2 to 3
        // Add SIMD capabilities if not already set
        if ((Capabilities & EMaterialCapabilities::SupportsSSE) == EMaterialCapabilities::None)
        {
            // Add basic SIMD support to existing materials
            Capabilities |= EMaterialCapabilities::SupportsSSE;
        }
        
        // Update to schema 3
        SchemaVersion = 3;
    }
    
    return bSuccess;
}

// Implementation of the MigrateToCurrentVersion method for relationship schema upgrades
bool FMaterialRelationship::MigrateToCurrentVersion(uint32 CurrentSchemaVersion)
{
    // Nothing to migrate if already at current version
    if (SchemaVersion >= CurrentSchemaVersion)
    {
        return true;
    }
    
    bool bSuccess = true;
    
    // Perform migrations based on the current schema version
    if (SchemaVersion < 2 && CurrentSchemaVersion >= 2)
    {
        // Migration from schema 1 to 2
        // In this example, schema 2 added support for BlendSharpness
        if (bCanBlend && FMath::IsNearlyEqual(BlendSharpness, 0.0f))
        {
            // Set a default blend sharpness for old relationships
            BlendSharpness = 0.5f;
        }
        
        // Update to schema 2
        SchemaVersion = 2;
    }
    
    // Additional migrations for future schema versions
    if (SchemaVersion < 3 && CurrentSchemaVersion >= 3)
    {
        // Migration from schema 2 to 3
        // Add interaction priority if not set
        if (InteractionPriority == 0)
        {
            // Default to a medium priority
            InteractionPriority = 50;
        }
        
        // Update to schema 3
        SchemaVersion = 3;
    }
    
    return bSuccess;
}

/**
 * Allocates channel memory for material type using NarrowBandAllocator
 * Integrates with memory management system to optimize material storage
 * @param TypeInfo Material type information
 */
void FMaterialRegistry::AllocateChannelMemory(const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>& TypeInfo)
{
    // Resolve the memory manager
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        UE_LOG(LogMaterialRegistry, Warning, TEXT("Failed to allocate channel memory for '%s' - Memory Manager not available"), 
            *TypeInfo->TypeName.ToString());
        return;
    }
    
    // Get compression utility
    FCompressionUtility* CompressionUtil = IServiceLocator::Get().ResolveService<FCompressionUtility>();
    
    // Find NarrowBandAllocator service
    FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPool(FName("HighPrecisionNBPool")));
    
    if (!NBAllocator)
    {
        // Try medium precision pool if high precision is not available
        NBAllocator = static_cast<FNarrowBandAllocator*>(
            MemoryManager->GetPool(FName("MediumPrecisionNBPool")));
    }
    
    if (!NBAllocator)
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("Failed to allocate channel memory for '%s' - NarrowBandAllocator not available"), 
            *TypeInfo->TypeName.ToString());
        return;
    }
    
    // Determine the material properties to determine channel characteristics
    float Density = 1.0f;
    float Hardness = 5.0f;
    bool bIsFluid = false;
    bool bIsGranular = false;
    bool bIsMultiLayered = false;
    
    // Get density property
    TSharedPtr<FMaterialPropertyBase> DensityProp = GetMaterialProperty(TypeInfo->TypeId, FName("Density"));
    if (DensityProp)
    {
        TSharedPtr<FMaterialProperty<float>> DensityFloatProp = StaticCastSharedPtr<FMaterialProperty<float>>(DensityProp);
        if (DensityFloatProp)
        {
            Density = DensityFloatProp->Value;
            
            // Check if it's a fluid (low density)
            bIsFluid = (Density < 1.2f);
        }
    }
    
    // Get hardness property
    TSharedPtr<FMaterialPropertyBase> HardnessProp = GetMaterialProperty(TypeInfo->TypeId, FName("Hardness"));
    if (HardnessProp)
    {
        TSharedPtr<FMaterialProperty<float>> HardnessFloatProp = StaticCastSharedPtr<FMaterialProperty<float>>(HardnessProp);
        if (HardnessFloatProp)
        {
            Hardness = HardnessFloatProp->Value;
            
            // Check if it's granular (low hardness)
            bIsGranular = (Hardness < 2.5f);
        }
    }
    
    // Get material type category
    const FName& Category = TypeInfo->Category;
    if (Category == FName("Layered") || Category == FName("Composite"))
    {
        bIsMultiLayered = true;
    }
    
    // Configure compression strategy based on material properties
    FMaterialCompressionSettings CompressionSettings;
    CompressionSettings.MaterialName = TypeInfo->TypeName;
    
    if (bIsFluid)
    {
        // Fluids need high precision for simulation
        CompressionSettings.CompressionLevel = EMaterialCompressionLevel::Low;
        CompressionSettings.bEnableAdaptivePrecision = true;
        CompressionSettings.bEnableLosslessMode = false;
    }
    else if (bIsGranular)
    {
        // Granular materials need medium compression
        CompressionSettings.CompressionLevel = EMaterialCompressionLevel::Medium;
        CompressionSettings.bEnableAdaptivePrecision = true;
        CompressionSettings.bEnableLosslessMode = false;
    }
    else if (bIsMultiLayered)
    {
        // Multi-layered materials need special compression
        CompressionSettings.CompressionLevel = EMaterialCompressionLevel::Custom;
        CompressionSettings.bEnableAdaptivePrecision = false;
        CompressionSettings.bEnableLosslessMode = true;
    }
    else
    {
        // Standard material compression
        CompressionSettings.CompressionLevel = EMaterialCompressionLevel::Medium;
        CompressionSettings.bEnableAdaptivePrecision = false;
        CompressionSettings.bEnableLosslessMode = false;
    }
    
    // Register compression settings with compression utility if available
    if (CompressionUtil)
    {
        CompressionUtil->RegisterMaterialCompression(TypeInfo->TypeId, CompressionSettings);
        UE_LOG(LogMaterialRegistry, Log, TEXT("Registered compression settings for material '%s'"), 
            *TypeInfo->TypeName.ToString());
    }
    
    // Determine channel configuration based on material properties
    uint32 ChannelCount = 1;  // Base material channel
    
    if (bIsMultiLayered)
    {
        ChannelCount += 2;  // Extra channels for layering
    }
    
    if (bIsFluid)
    {
        ChannelCount += 1;  // Extra channel for fluid simulation
    }
    
    // Allocate material channel memory
    int32 ChannelId = NBAllocator->AllocateChannelMemory(
        TypeInfo->TypeId,
        ChannelCount,
        bIsFluid ? EMemoryTier::Hot : EMemoryTier::Warm,
        CompressionSettings.CompressionLevel
    );
    
    if (ChannelId >= 0)
    {
        UE_LOG(LogMaterialRegistry, Log, TEXT("Allocated %u channels for material '%s' (Channel ID: %d)"), 
            ChannelCount, *TypeInfo->TypeName.ToString(), ChannelId);
            
        // Store channel ID in material info for future reference
        TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* MutableInfo = GetMutableMaterialTypeInfo(TypeInfo->TypeId);
        if (MutableInfo)
        {
            (*MutableInfo)->ChannelId = ChannelId;
            (*MutableInfo)->ChannelCount = ChannelCount;
        }
        
        // Set up memory sharing for related materials if this is a derived type
        SetupMemorySharingForDerivedMaterials(TypeInfo->TypeId);
    }
    else
    {
        UE_LOG(LogMaterialRegistry, Error, TEXT("Failed to allocate channel memory for material '%s'"), 
            *TypeInfo->TypeName.ToString());
    }
}

/**
 * Sets up memory sharing between related material types
 * Optimizes memory usage by sharing channels between parent and child materials
 * @param TypeId Material type ID to set up sharing for
 */
void FMaterialRegistry::SetupMemorySharingForDerivedMaterials(uint32 TypeId)
{
    const FMaterialTypeInfo* TypeInfo = GetMaterialTypeInfo(TypeId);
    if (!TypeInfo || TypeInfo->ParentTypeId == 0)
    {
        return; // Not a derived material
    }
    
    const FMaterialTypeInfo* ParentTypeInfo = GetMaterialTypeInfo(TypeInfo->ParentTypeId);
    if (!ParentTypeInfo || ParentTypeInfo->ChannelId < 0)
    {
        return; // Parent has no channel allocation
    }
    
    // Resolve the memory manager
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        return;
    }
    
    // Find NarrowBandAllocator service
    FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPool(FName("HighPrecisionNBPool")));
    
    if (!NBAllocator)
    {
        // Try medium precision pool if high precision is not available
        NBAllocator = static_cast<FNarrowBandAllocator*>(
            MemoryManager->GetPool(FName("MediumPrecisionNBPool")));
    }
    
    if (!NBAllocator)
    {
        return;
    }
    
    // Set up memory sharing between parent and child material
    bool bSuccess = NBAllocator->SetupSharedChannels(
        TypeInfo->TypeId, 
        ParentTypeInfo->TypeId,
        TypeInfo->ChannelId,
        ParentTypeInfo->ChannelId
    );
    
    if (bSuccess)
    {
        UE_LOG(LogMaterialRegistry, Log, TEXT("Set up shared memory channels between '%s' and parent '%s'"),
            *TypeInfo->TypeName.ToString(), *ParentTypeInfo->TypeName.ToString());
    }
}

ERegistryType FMaterialRegistry::GetRegistryType() const
{
    return ERegistryType::Material;
}

ETypeCapabilities FMaterialRegistry::GetTypeCapabilities(uint32 TypeId) const
{
    // Start with no capabilities
    ETypeCapabilities Capabilities = ETypeCapabilities::None;
    
    // Check if this type is registered
    if (!IsMaterialTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the material type info
    const FMaterialTypeInfo* TypeInfo = GetMaterialTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map material capabilities to type capabilities
    if (static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsMultiThreading))
    {
        Capabilities |= ETypeCapabilities::ThreadSafe;
    }
    
    if (static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsSSE) ||
        static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsAVX) ||
        static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsAVX2) ||
        static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsNeon))
    {
        Capabilities |= ETypeCapabilities::SIMDOperations;
    }
    
    if (static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsIncrementalUpdates))
    {
        Capabilities |= ETypeCapabilities::IncrementalUpdates;
    }
    
    if (static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsDynamicRehierarchization))
    {
        Capabilities |= ETypeCapabilities::CacheOptimized;
    }
    
    if (static_cast<bool>(TypeInfo->Capabilities & EMaterialCapabilities::SupportsGPUCompute))
    {
        Capabilities |= ETypeCapabilities::Vectorizable;
    }
    
    return Capabilities;
}

uint64 FMaterialRegistry::ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config)
{
    // Create a type-specific task configuration
    FTaskConfig TypedConfig = Config;
    TypedConfig.SetTypeId(TypeId, ERegistryType::Material);
    
    // Set optimization flags based on type capabilities
    ETypeCapabilities Capabilities = GetTypeCapabilities(TypeId);
    EThreadOptimizationFlags OptimizationFlags = EThreadOptimizationFlags::None;
    
    if (EnumHasAnyFlags(Capabilities, ETypeCapabilities::SIMDOperations))
    {
        OptimizationFlags |= EThreadOptimizationFlags::SIMDAware;
    }
    
    if (EnumHasAnyFlags(Capabilities, ETypeCapabilities::CacheOptimized))
    {
        OptimizationFlags |= EThreadOptimizationFlags::CacheLocality;
    }
    
    if (EnumHasAnyFlags(Capabilities, ETypeCapabilities::ParallelProcessing))
    {
        OptimizationFlags |= EThreadOptimizationFlags::ComputeIntensive;
    }
    
    TypedConfig.SetOptimizationFlags(OptimizationFlags);
    
    // Schedule the task with the scheduler
    return ScheduleTaskWithScheduler(TaskFunc, TypedConfig);
}

// Let's add the NUMA-aware method implementations

const FMaterialTypeInfo* FMaterialRegistry::GetMaterialTypeInfoNUMAOptimized(uint32 InTypeId) const
{
    if (InTypeId == 0)
    {
        return nullptr;
    }
    
    // Get current NUMA domain
    uint32 CurrentDomainId = FThreadSafety::Get().GetCurrentThreadNUMADomain();
    
    // Try to get from domain-local cache using regular lookup method first, avoiding template issues
    FNUMALocalTypeCache* DomainCache = FThreadSafety::Get().GetOrCreateDomainTypeCache(CurrentDomainId);
    if (!DomainCache)
    {
        // Fall back to normal lookup
        const FMaterialTypeInfo* TypeInfo = GetMaterialTypeInfo(InTypeId);
        RecordMaterialTypeAccess(InTypeId, 0, false);
        return TypeInfo;
    }
    
    // Get current version
    uint32 CurrentVersion = 0;
    {
        FScopedSpinLock Lock(RegistryLock);
        if (MaterialTypeMap.Contains(InTypeId))
        {
            CurrentVersion = MaterialTypeMap[InTypeId]->SchemaVersion;
        }
    }
    
    // Normal lookup path, skipping the problematic template methods
    const FMaterialTypeInfo* TypeInfo = GetMaterialTypeInfo(InTypeId);
    
    // Record this access for optimization purposes
    RecordMaterialTypeAccess(InTypeId, 0, false);
    
    return TypeInfo;
}

bool FMaterialRegistry::SetPreferredNUMADomainForType(uint32 InTypeId, uint32 DomainId)
{
    if (InTypeId == 0)
    {
        return false;
    }
    
    // Validate that the domain exists
    FNUMATopology& Topology = FThreadSafety::Get().NUMATopology;
    if (DomainId >= Topology.DomainCount)
    {
        return false;
    }
    
    // Check if the type exists
    {
        FScopedSpinLock Lock(RegistryLock);
        if (!MaterialTypeMap.Contains(InTypeId))
        {
            return false;
        }
    }
    
    // Set the preferred domain
    {
        FScopeLock Lock(&NumaDomainLock);
        TypeNUMADomainPreferences.Add(InTypeId, DomainId);
    }
    
    return true;
}

uint32 FMaterialRegistry::GetPreferredNUMADomainForType(uint32 InTypeId) const
{
    if (InTypeId == 0)
    {
        return MAX_uint32;
    }
    
    FScopeLock Lock(&NumaDomainLock);
    return TypeNUMADomainPreferences.Contains(InTypeId) ? TypeNUMADomainPreferences[InTypeId] : MAX_uint32;
}

void FMaterialRegistry::PrefetchTypesToDomain(const TArray<uint32>& TypeIds, uint32 DomainId)
{
    FNUMATopology& Topology = FThreadSafety::Get().NUMATopology;
    if (DomainId >= Topology.DomainCount)
    {
        return;
    }
    
    // Get the domain cache but don't attempt to use the problematic template methods
    FNUMALocalTypeCache* DomainCache = FThreadSafety::Get().GetOrCreateDomainTypeCache(DomainId);
    if (!DomainCache)
    {
        return;
    }
    
    // Just mark the types as preferring this domain
    for (uint32 TypeId : TypeIds)
    {
        if (TypeId == 0)
        {
            continue;
        }
        
        // Set the preferred domain for this type
        SetPreferredNUMADomainForType(TypeId, DomainId);
    }
}

void FMaterialRegistry::RecordMaterialTypeAccess(uint32 InTypeId, uint32 ThreadId, bool bIsWrite) const
{
    if (InTypeId == 0)
    {
        return;
    }
    
    // If thread ID is 0, use current thread
    if (ThreadId == 0)
    {
        ThreadId = FPlatformTLS::GetCurrentThreadId();
    }
    
    // Get the NUMA domain for this thread
    uint32 DomainId = FThreadSafety::Get().GetCurrentThreadNUMADomain();
    
    // Record the access
    {
        FScopeLock Lock(&TypeAccessLock);
        
        // Ensure maps exist
        if (!TypeAccessByDomain.Contains(InTypeId))
        {
            TypeAccessByDomain.Add(InTypeId, TMap<uint32, uint32>());
        }
        
        // Increment access count for this domain
        uint32& AccessCount = TypeAccessByDomain[InTypeId].FindOrAdd(DomainId);
        AccessCount++;
    }
}

TMap<uint32, FString> FMaterialRegistry::GetNUMAAccessStats() const
{
    TMap<uint32, FString> Results;
    
    FScopeLock Lock(&TypeAccessLock);
    
    // Build stats for each domain
    TMap<uint32, int32> DomainAccessCounts;
    TMap<uint32, TArray<uint32>> DomainTopTypes;
    
    for (const auto& TypePair : TypeAccessByDomain)
    {
        uint32 TypeId = TypePair.Key;
        const TMap<uint32, uint32>& DomainAccesses = TypePair.Value;
        
        // Find domain with most accesses for this type
        uint32 BestDomainId = 0;
        uint32 HighestAccess = 0;
        
        for (const auto& DomainPair : DomainAccesses)
        {
            uint32 DomainId = DomainPair.Key;
            uint32 AccessCount = DomainPair.Value;
            
            // Update total count for this domain
            int32& TotalCount = DomainAccessCounts.FindOrAdd(DomainId);
            TotalCount += AccessCount;
            
            // Check if this is the highest access count
            if (AccessCount > HighestAccess)
            {
                HighestAccess = AccessCount;
                BestDomainId = DomainId;
            }
        }
        
        // Add to domain's top types list
        if (HighestAccess > 0)
        {
            if (!DomainTopTypes.Contains(BestDomainId))
            {
                DomainTopTypes.Add(BestDomainId, TArray<uint32>());
            }
            
            if (DomainTopTypes[BestDomainId].Num() < 10)
            {
                DomainTopTypes[BestDomainId].Add(TypeId);
            }
        }
    }
    
    // Format results
    for (const auto& DomainPair : DomainAccessCounts)
    {
        uint32 DomainId = DomainPair.Key;
        int32 TotalAccesses = DomainPair.Value;
        
        FString TypeList;
        if (DomainTopTypes.Contains(DomainId))
        {
            for (int32 i = 0; i < DomainTopTypes[DomainId].Num(); ++i)
            {
                uint32 TypeId = DomainTopTypes[DomainId][i];
                
                // Get type name for better readability
                FString TypeName = TEXT("Unknown");
                
                FScopedSpinLock RegistryLockLocal(RegistryLock);
                if (MaterialTypeMap.Contains(TypeId))
                {
                    TypeName = MaterialTypeMap[TypeId]->TypeName.ToString();
                }
                
                TypeList += FString::Printf(TEXT("%s%s(%u)"), i > 0 ? TEXT(", ") : TEXT(""), *TypeName, TypeId);
            }
        }
        
        Results.Add(DomainId, FString::Printf(TEXT("Domain %u: %d accesses, Top Types: [%s]"), 
            DomainId, TotalAccesses, *TypeList));
    }
    
    return Results;
}

int32 FMaterialRegistry::OptimizeTypeNUMAPlacement()
{
    int32 TypesMigrated = 0;
    
    FScopeLock AccessLock(&TypeAccessLock);
    
    // Analyze access patterns
    for (const auto& TypePair : TypeAccessByDomain)
    {
        uint32 TypeId = TypePair.Key;
        const TMap<uint32, uint32>& DomainAccesses = TypePair.Value;
        
        // Find domain with most accesses
        uint32 BestDomainId = 0;
        uint32 HighestAccess = 0;
        uint32 TotalAccesses = 0;
        
        for (const auto& DomainPair : DomainAccesses)
        {
            uint32 DomainId = DomainPair.Key;
            uint32 AccessCount = DomainPair.Value;
            
            TotalAccesses += AccessCount;
            
            if (AccessCount > HighestAccess)
            {
                HighestAccess = AccessCount;
                BestDomainId = DomainId;
            }
        }
        
        // Only migrate if access pattern is significant enough
        if (TotalAccesses > 100 && HighestAccess > TotalAccesses * 0.6f)
        {
            uint32 CurrentDomain = GetPreferredNUMADomainForType(TypeId);
            
            // If no current preference or different from best domain, update
            if (CurrentDomain == MAX_uint32 || CurrentDomain != BestDomainId)
            {
                if (SetPreferredNUMADomainForType(TypeId, BestDomainId))
                {
                    TypesMigrated++;
                    
                    // Prefetch to the new domain
                    TArray<uint32> TypeIdsToMigrate;
                    TypeIdsToMigrate.Add(TypeId);
                    PrefetchTypesToDomain(TypeIdsToMigrate, BestDomainId);
                }
            }
        }
    }
    
    return TypesMigrated;
}
