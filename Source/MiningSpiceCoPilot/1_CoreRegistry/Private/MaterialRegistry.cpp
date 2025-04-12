// Copyright Epic Games, Inc. All Rights Reserved.

#include "1_CoreRegistry/Public/MaterialRegistry.h"

// Initialize static members
FMaterialRegistry* FMaterialRegistry::Singleton = nullptr;
FThreadSafeBool FMaterialRegistry::bSingletonInitialized = false;

FMaterialRegistry::FMaterialRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , NextRelationshipId(1) // Reserve 0 as invalid/unregistered relationship ID
    , NextChannelId(0) // Start channel IDs at 0
    , bIsInitialized(false)
    , SchemaVersion(1) // Initial schema version
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
    bool bWasAlreadyInitialized = false;
    if (!bIsInitialized.AtomicSet(true, bWasAlreadyInitialized))
    {
        // Initialize internal maps
        MaterialTypeMap.Empty();
        MaterialTypeNameMap.Empty();
        RelationshipMap.Empty();
        RelationshipsBySourceMap.Empty();
        RelationshipsByTargetMap.Empty();
        
        // Reset counters
        NextTypeId = 1;
        NextRelationshipId = 1;
        NextChannelId = 0;
        
        return true;
    }
    
    // Already initialized
    return false;
}

void FMaterialRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopeLock Lock(&RegistryLock);
        
        // Clear all registered items
        MaterialTypeMap.Empty();
        MaterialTypeNameMap.Empty();
        RelationshipMap.Empty();
        RelationshipsBySourceMap.Empty();
        RelationshipsByTargetMap.Empty();
        
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
    return SchemaVersion;
}

bool FMaterialRegistry::Validate(TArray<FString>& OutErrors) const
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Material Registry is not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    bool bIsValid = true;
    
    // Validate material type map integrity
    for (const auto& TypeNamePair : MaterialTypeNameMap)
    {
        const FName& TypeName = TypeNamePair.Key;
        const uint32 TypeId = TypeNamePair.Value;
        
        if (!MaterialTypeMap.Contains(TypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Material type name '%s' references non-existent type ID %u"), *TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        else if (MaterialTypeMap[TypeId]->TypeName != TypeName)
        {
            OutErrors.Add(FString::Printf(TEXT("Material type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *TypeName.ToString(), TypeId, *MaterialTypeMap[TypeId]->TypeName.ToString()));
            bIsValid = false;
        }
    }
    
    // Validate parent-child relationships
    for (const auto& TypeInfoPair : MaterialTypeMap)
    {
        const uint32 TypeId = TypeInfoPair.Key;
        const TSharedRef<FMaterialTypeInfo>& TypeInfo = TypeInfoPair.Value;
        
        // Check if parent type exists (if specified)
        if (TypeInfo->ParentTypeId != 0)
        {
            if (!MaterialTypeMap.Contains(TypeInfo->ParentTypeId))
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
            for (const auto& OtherTypePair : MaterialTypeMap)
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
        if (!MaterialTypeMap.Contains(Relationship->SourceTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Material relationship (ID %u) references non-existent source type ID %u"),
                RelationshipId, Relationship->SourceTypeId));
            bIsValid = false;
        }
        
        if (!MaterialTypeMap.Contains(Relationship->TargetTypeId))
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
    for (auto It = RelationshipsBySourceMap.CreateConstIterator(); It; ++It)
    {
        const uint32 SourceTypeId = It.Key();
        const uint32 RelationshipId = It.Value();
        
        // Verify source type exists
        if (!MaterialTypeMap.Contains(SourceTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsBySourceMap contains non-existent source type ID %u"),
                SourceTypeId));
            bIsValid = false;
        }
        
        // Verify relationship exists
        if (!RelationshipMap.Contains(RelationshipId))
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsBySourceMap references non-existent relationship ID %u"),
                RelationshipId));
            bIsValid = false;
        }
        else if (RelationshipMap[RelationshipId]->SourceTypeId != SourceTypeId)
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsBySourceMap inconsistency: relationship %u source is %u, not %u"),
                RelationshipId, RelationshipMap[RelationshipId]->SourceTypeId, SourceTypeId));
            bIsValid = false;
        }
    }
    
    // Similar verification for target map
    for (auto It = RelationshipsByTargetMap.CreateConstIterator(); It; ++It)
    {
        const uint32 TargetTypeId = It.Key();
        const uint32 RelationshipId = It.Value();
        
        // Verify target type exists
        if (!MaterialTypeMap.Contains(TargetTypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsByTargetMap contains non-existent target type ID %u"),
                TargetTypeId));
            bIsValid = false;
        }
        
        // Verify relationship exists
        if (!RelationshipMap.Contains(RelationshipId))
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsByTargetMap references non-existent relationship ID %u"),
                RelationshipId));
            bIsValid = false;
        }
        else if (RelationshipMap[RelationshipId]->TargetTypeId != TargetTypeId)
        {
            OutErrors.Add(FString::Printf(TEXT("RelationshipsByTargetMap inconsistency: relationship %u target is %u, not %u"),
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
        FScopeLock Lock(&RegistryLock);
        
        // Clear all registered items
        MaterialTypeMap.Empty();
        MaterialTypeNameMap.Empty();
        RelationshipMap.Empty();
        RelationshipsBySourceMap.Empty();
        RelationshipsByTargetMap.Empty();
        
        // Reset counters
        NextTypeId = 1;
        NextRelationshipId = 1;
        NextChannelId = 0;
    }
}

uint32 FMaterialRegistry::RegisterMaterialType(
    const FName& InTypeName,
    EMaterialPriority InPriority,
    const FName& InParentTypeName)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialType failed - registry not initialized"));
        return 0;
    }
    
    if (InTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialType failed - invalid type name"));
        return 0;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    // Check if type name is already registered
    if (MaterialTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FMaterialRegistry::RegisterMaterialType - type '%s' is already registered"),
            *InTypeName.ToString());
        return 0;
    }
    
    // Find parent type ID if specified
    uint32 ParentTypeId = 0;
    if (!InParentTypeName.IsNone())
    {
        const uint32* ParentIdPtr = MaterialTypeNameMap.Find(InParentTypeName);
        if (!ParentIdPtr)
        {
            UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialType failed - parent type '%s' not found"),
                *InParentTypeName.ToString());
            return 0;
        }
        
        ParentTypeId = *ParentIdPtr;
    }
    
    // Generate a unique type ID
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create and populate type info
    TSharedRef<FMaterialTypeInfo> TypeInfo = MakeShared<FMaterialTypeInfo>();
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->ParentTypeId = ParentTypeId;
    TypeInfo->Priority = InPriority;
    
    // Set default values
    TypeInfo->ResourceValueMultiplier = 1.0f;
    TypeInfo->BaseMiningResistance = 1.0f;
    TypeInfo->SoundAmplificationFactor = 1.0f;
    TypeInfo->ParticleEmissionMultiplier = 1.0f;
    TypeInfo->bIsMineable = true;
    TypeInfo->bIsResource = false;
    TypeInfo->bCanFracture = true;
    TypeInfo->ChannelId = -1; // No channel assigned by default
    
    // Register the type
    MaterialTypeMap.Add(TypeId, TypeInfo);
    MaterialTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::RegisterMaterialType - registered type '%s' with ID %u"),
        *InTypeName.ToString(), TypeId);
    
    return TypeId;
}

uint32 FMaterialRegistry::RegisterMaterialRelationship(
    const FName& InSourceTypeName,
    const FName& InTargetTypeName,
    float InCompatibilityScore,
    bool bInCanBlend)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialRelationship failed - registry not initialized"));
        return 0;
    }
    
    if (InSourceTypeName.IsNone() || InTargetTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialRelationship failed - invalid type names"));
        return 0;
    }
    
    // Clamp compatibility score to valid range
    float CompatibilityScore = FMath::Clamp(InCompatibilityScore, 0.0f, 1.0f);
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    // Look up source and target type IDs
    const uint32* SourceIdPtr = MaterialTypeNameMap.Find(InSourceTypeName);
    if (!SourceIdPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialRelationship failed - source type '%s' not found"),
            *InSourceTypeName.ToString());
        return 0;
    }
    
    const uint32* TargetIdPtr = MaterialTypeNameMap.Find(InTargetTypeName);
    if (!TargetIdPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FMaterialRegistry::RegisterMaterialRelationship failed - target type '%s' not found"),
            *InTargetTypeName.ToString());
        return 0;
    }
    
    // Check for existing relationship
    TArray<uint32> ExistingRelationships;
    RelationshipsBySourceMap.MultiFind(*SourceIdPtr, ExistingRelationships);
    
    for (uint32 RelationshipId : ExistingRelationships)
    {
        const FMaterialRelationship& Relationship = RelationshipMap[RelationshipId].Get();
        if (Relationship.TargetTypeId == *TargetIdPtr)
        {
            UE_LOG(LogTemp, Warning, TEXT("FMaterialRegistry::RegisterMaterialRelationship - relationship between '%s' and '%s' already exists"),
                *InSourceTypeName.ToString(), *InTargetTypeName.ToString());
            return 0;
        }
    }
    
    // Generate a unique relationship ID
    uint32 RelationshipId = GenerateUniqueRelationshipId();
    
    // Create and populate relationship info
    TSharedRef<FMaterialRelationship> RelationshipInfo = MakeShared<FMaterialRelationship>();
    RelationshipInfo->RelationshipId = RelationshipId;
    RelationshipInfo->SourceTypeId = *SourceIdPtr;
    RelationshipInfo->TargetTypeId = *TargetIdPtr;
    RelationshipInfo->CompatibilityScore = CompatibilityScore;
    RelationshipInfo->bCanBlend = bInCanBlend;
    RelationshipInfo->BlendSharpness = 0.5f; // Default blend sharpness
    
    // Register the relationship
    RelationshipMap.Add(RelationshipId, RelationshipInfo);
    RelationshipsBySourceMap.Add(*SourceIdPtr, RelationshipId);
    RelationshipsByTargetMap.Add(*TargetIdPtr, RelationshipId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FMaterialRegistry::RegisterMaterialRelationship - registered relationship between '%s' and '%s' with ID %u"),
        *InSourceTypeName.ToString(), *InTargetTypeName.ToString(), RelationshipId);
    
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
    FScopeLock Lock(&RegistryLock);
    
    // Check if the material type is registered
    TSharedRef<FMaterialTypeInfo>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
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
    
    // Allocate a new channel ID
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up type by ID
    TSharedRef<FMaterialTypeInfo>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = MaterialTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        TSharedRef<FMaterialTypeInfo>* TypeInfoPtr = MaterialTypeMap.Find(*TypeIdPtr);
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
    FScopeLock Lock(&RegistryLock);
    
    // Look up relationship by ID
    TSharedRef<FMaterialRelationship>* RelationshipPtr = RelationshipMap.Find(InRelationshipId);
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
    FScopeLock Lock(&RegistryLock);
    
    // Collect all material type infos
    Result.Reserve(MaterialTypeMap.Num());
    for (const auto& TypeInfoPair : MaterialTypeMap)
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
    FScopeLock Lock(&RegistryLock);
    
    // Check if the parent type is registered
    if (!MaterialTypeMap.Contains(InParentTypeId))
    {
        return Result;
    }
    
    // Collect all material types that have this parent
    for (const auto& TypeInfoPair : MaterialTypeMap)
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
    FScopeLock Lock(&RegistryLock);
    
    // Collect all relationships where this type is the source
    TArray<uint32> RelationshipIds;
    RelationshipsBySourceMap.MultiFind(InTypeId, RelationshipIds);
    
    for (uint32 RelationshipId : RelationshipIds)
    {
        const TSharedRef<FMaterialRelationship>* RelationshipPtr = RelationshipMap.Find(RelationshipId);
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
    FScopeLock Lock(&RegistryLock);
    
    return MaterialTypeMap.Contains(InTypeId);
}

bool FMaterialRegistry::IsMaterialTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
    return MaterialTypeNameMap.Contains(InTypeName);
}

bool FMaterialRegistry::IsMaterialDerivedFrom(uint32 InDerivedTypeId, uint32 InBaseTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&RegistryLock);
    
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
        const TSharedRef<FMaterialTypeInfo>* TypeInfoPtr = MaterialTypeMap.Find(CurrentTypeId);
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
    FScopeLock Lock(&RegistryLock);
    
    // Check if the material type is registered
    TSharedRef<FMaterialTypeInfo>* TypeInfoPtr = MaterialTypeMap.Find(InTypeId);
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

FMaterialRegistry& FMaterialRegistry::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        bool bWasAlreadyInitialized = false;
        if (!bSingletonInitialized.AtomicSet(true, bWasAlreadyInitialized))
        {
            Singleton = new FMaterialRegistry();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

uint32 FMaterialRegistry::GenerateUniqueTypeId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextTypeId++;
}

uint32 FMaterialRegistry::GenerateUniqueRelationshipId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextRelationshipId++;
}