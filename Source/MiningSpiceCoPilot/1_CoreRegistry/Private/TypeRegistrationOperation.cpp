// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypeRegistrationOperation.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"

// Define operation type strings
static const FString ZoneTypeRegistrationOperationType = TEXT("ZoneTypeRegistration");
static const FString MaterialTypeRegistrationOperationType = TEXT("MaterialTypeRegistration");
static const FString SDFFieldTypeRegistrationOperationType = TEXT("SDFFieldTypeRegistration");
static const FString SDFOperationsRegistrationOperationType = TEXT("SDFOperationsRegistration");
static const FString SVONodeTypeRegistrationOperationType = TEXT("SVONodeTypeRegistration");

// ====================================================================================================
// FTypeRegistrationOperation Implementation
// ====================================================================================================

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, ETypeRegistrationRegistry InRegistryType, const FString& InSourceAsset)
    : FAsyncOperationImpl(InId, GetOperationTypeForRegistry(InRegistryType), InName)
    , RegistryType(InRegistryType)
    , SourceAsset(InSourceAsset)
    , bUsingSourceAsset(true)
    , bCancelled(false)
{
}

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FZoneTransactionTypeInfo>& InTypes)
    : FAsyncOperationImpl(InId, ZoneTypeRegistrationOperationType, InName)
    , RegistryType(ETypeRegistrationRegistry::Zone)
    , ZoneTypes(InTypes)
    , bUsingSourceAsset(false)
    , bCancelled(false)
{
    TypeProgress.TotalTypes = ZoneTypes.Num();
}

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FMaterialTypeInfo>& InTypes)
    : FAsyncOperationImpl(InId, MaterialTypeRegistrationOperationType, InName)
    , RegistryType(ETypeRegistrationRegistry::Material)
    , MaterialTypes(InTypes)
    , bUsingSourceAsset(false)
    , bCancelled(false)
{
    TypeProgress.TotalTypes = MaterialTypes.Num();
}

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSDFFieldTypeInfo>& InTypes)
    : FAsyncOperationImpl(InId, SDFFieldTypeRegistrationOperationType, InName)
    , RegistryType(ETypeRegistrationRegistry::SDF)
    , SDFFieldTypes(InTypes)
    , bUsingSourceAsset(false)
    , bCancelled(false)
{
    TypeProgress.TotalTypes = SDFFieldTypes.Num();
}

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSDFOperationInfo>& InOperations)
    : FAsyncOperationImpl(InId, SDFOperationsRegistrationOperationType, InName)
    , RegistryType(ETypeRegistrationRegistry::SDF)
    , SDFOperations(InOperations)
    , bUsingSourceAsset(false)
    , bCancelled(false)
{
    TypeProgress.TotalTypes = SDFOperations.Num();
}

FTypeRegistrationOperation::FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSVONodeTypeInfo>& InTypes)
    : FAsyncOperationImpl(InId, SVONodeTypeRegistrationOperationType, InName)
    , RegistryType(ETypeRegistrationRegistry::SVO)
    , SVONodeTypes(InTypes)
    , bUsingSourceAsset(false)
    , bCancelled(false)
{
    TypeProgress.TotalTypes = SVONodeTypes.Num();
}

FTypeRegistrationOperation::~FTypeRegistrationOperation()
{
}

bool FTypeRegistrationOperation::Execute()
{
    // Check if we're using an asset file as the source
    if (bUsingSourceAsset)
    {
        bool OperationResult = ExtractTypesFromSourceAsset();
        if (!OperationResult)
        {
            // Failed to extract types from source asset
            return false;
        }
    }
    
    // Set initial progress
    FAsyncProgress InitialProgress;
    InitialProgress.StatusMessage = FString::Printf(TEXT("Starting type registration for %s"), *Name);
    InitialProgress.CompletionPercentage = 0.0f;
    InitialProgress.TotalItems = TypeProgress.TotalTypes;
    UpdateProgress(InitialProgress);
    
    // Set operation start time
    SetStartTime(FPlatformTime::Seconds());
    
    // Begin execution based on operation type
    bool bSuccess = false;
    
    if (bUsingSourceAsset)
    {
        bSuccess = ExecuteSourceAssetRegistration();
    }
    else
    {
        switch (RegistryType)
        {
            case ETypeRegistrationRegistry::Zone:
                bSuccess = ExecuteZoneTypeBatchRegistration();
                break;
                
            case ETypeRegistrationRegistry::Material:
                bSuccess = ExecuteMaterialTypeBatchRegistration();
                break;
                
            case ETypeRegistrationRegistry::SDF:
                if (SDFOperations.Num() > 0)
                {
                    bSuccess = ExecuteSDFOperationsBatchRegistration();
                }
                else
                {
                    bSuccess = ExecuteSDFFieldTypeBatchRegistration();
                }
                break;
                
            case ETypeRegistrationRegistry::SVO:
                bSuccess = ExecuteSVONodeTypeBatchRegistration();
                break;
        }
    }
    
    // Set completion state
    FAsyncResult OperationResult;
    if (bCancelled)
    {
        OperationResult = FAsyncResult::Cancelled();
        SetStatus(EAsyncStatus::Cancelled);
    }
    else if (bSuccess)
    {
        OperationResult.bSuccess = true;
        OperationResult.ErrorMessage = TEXT("");
        OperationResult.ErrorCode = 0;
        SetStatus(EAsyncStatus::Completed);
    }
    else
    {
        FString ErrorSummary = FString::Printf(TEXT("Type registration failed with %d errors: "), TypeProgress.ErrorMessages.Num());
        for (int32 i = 0; i < FMath::Min(3, TypeProgress.ErrorMessages.Num()); ++i)
        {
            ErrorSummary += TEXT("\n") + TypeProgress.ErrorMessages[i];
        }
        
        if (TypeProgress.ErrorMessages.Num() > 3)
        {
            ErrorSummary += TEXT("\n...and ") + FString::FromInt(TypeProgress.ErrorMessages.Num() - 3) + TEXT(" more errors");
        }
        
        OperationResult.bSuccess = false;
        OperationResult.ErrorMessage = ErrorSummary;
        OperationResult.ErrorCode = -1;
        SetStatus(EAsyncStatus::Failed);
    }
    
    // Set completion time
    SetCompletionTime(FPlatformTime::Seconds());
    
    // Set result and send notification
    SetResult(OperationResult);
    NotifyCompletion();
    
    return bSuccess;
}

bool FTypeRegistrationOperation::Cancel()
{
    bCancelled = true;
    return true;
}

bool FTypeRegistrationOperation::ExecuteSourceAssetRegistration()
{
    // First extract types from the source asset
    if (!ExtractTypesFromSourceAsset())
    {
        AddErrorMessage(FString::Printf(TEXT("Failed to extract types from source asset: %s"), *SourceAsset));
        return false;
    }
    
    // Now execute the appropriate batch registration based on registry type
    switch (RegistryType)
    {
        case ETypeRegistrationRegistry::Zone:
            return ExecuteZoneTypeBatchRegistration();
            
        case ETypeRegistrationRegistry::Material:
            return ExecuteMaterialTypeBatchRegistration();
            
        case ETypeRegistrationRegistry::SDF:
            if (SDFOperations.Num() > 0)
            {
                return ExecuteSDFOperationsBatchRegistration();
            }
            else
            {
                return ExecuteSDFFieldTypeBatchRegistration();
            }
            
        case ETypeRegistrationRegistry::SVO:
            return ExecuteSVONodeTypeBatchRegistration();
    }
    
    return false;
}

bool FTypeRegistrationOperation::ExecuteZoneTypeBatchRegistration()
{
    FZoneTypeRegistry& Registry = FZoneTypeRegistry::Get();
    TypeProgress.TotalTypes = ZoneTypes.Num();
    
    // Validate all types before registering
    TArray<FString> ValidationErrors;
    for (const FZoneTransactionTypeInfo& TypeInfo : ZoneTypes)
    {
        // Skip types that are already registered
        if (Registry.IsTransactionTypeRegistered(TypeInfo.TypeName))
        {
            continue;
        }
        
        // Basic validation
        if (TypeInfo.TypeName == NAME_None)
        {
            ValidationErrors.Add(FString::Printf(TEXT("Invalid type name for transaction type")));
        }
    }
    
    if (ValidationErrors.Num() > 0)
    {
        for (const FString& Error : ValidationErrors)
        {
            AddErrorMessage(Error);
        }
        return false;
    }
    
    // Register the types
    for (int32 i = 0; i < ZoneTypes.Num(); ++i)
    {
        // Check for cancellation
        if (bCancelled)
        {
            return false;
        }
        
        const FZoneTransactionTypeInfo& TypeInfo = ZoneTypes[i];
        
        // Skip already registered types
        if (Registry.IsTransactionTypeRegistered(TypeInfo.TypeName))
        {
            // Still count as processed for progress tracking
            TypeProgress.ProcessedTypes++;
            UpdateProgress();
            continue;
        }
        
        // Try to register the type
        uint32 TypeId = Registry.RegisterTransactionType(
            TypeInfo.TypeName,
            TypeInfo.ConcurrencyLevel,
            TypeInfo.RetryStrategy);
        
        if (TypeId == 0)
        {
            TypeProgress.FailedTypes++;
            AddErrorMessage(FString::Printf(TEXT("Failed to register transaction type: %s"), *TypeInfo.TypeName.ToString()));
        }
        else
        {
            // Update additional properties if needed
            Registry.UpdateTransactionProperty(TypeId, TEXT("RequiresVersionTracking"), 
                TypeInfo.bRequiresVersionTracking ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsFastPath"), 
                TypeInfo.bSupportsFastPath ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("FastPathThreshold"), 
                FString::Printf(TEXT("%f"), TypeInfo.FastPathThreshold));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("HasReadValidateWritePattern"), 
                TypeInfo.bHasReadValidateWritePattern ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsThreadSafeAccess"), 
                TypeInfo.bSupportsThreadSafeAccess ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsPartialProcessing"), 
                TypeInfo.bSupportsPartialProcessing ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsIncrementalUpdates"), 
                TypeInfo.bSupportsIncrementalUpdates ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("LowContention"), 
                TypeInfo.bLowContention ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsResultMerging"), 
                TypeInfo.bSupportsResultMerging ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsAsyncProcessing"), 
                TypeInfo.bSupportsAsyncProcessing ? TEXT("True") : TEXT("False"));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SchemaVersion"), 
                FString::Printf(TEXT("%u"), TypeInfo.SchemaVersion));
                
            Registry.UpdateTransactionProperty(TypeId, TEXT("SupportsPartialExecution"), 
                TypeInfo.bSupportsPartialExecution ? TEXT("True") : TEXT("False"));
        }
        
        TypeProgress.ProcessedTypes++;
        UpdateProgress();
        
        // Add small sleep to avoid blocking the thread completely
        FPlatformProcess::Sleep(0.001f);
    }
    
    return TypeProgress.FailedTypes == 0;
}

bool FTypeRegistrationOperation::ExecuteMaterialTypeBatchRegistration()
{
    FMaterialRegistry& Registry = FMaterialRegistry::Get();
    TypeProgress.TotalTypes = MaterialTypes.Num();
    
    // Validate all types before registering
    TArray<FString> ValidationErrors;
    for (const FMaterialTypeInfo& TypeInfo : MaterialTypes)
    {
        // Skip types that are already registered
        if (Registry.IsMaterialTypeRegistered(TypeInfo.TypeName))
        {
            continue;
        }
        
        // Basic validation
        if (TypeInfo.TypeName == NAME_None)
        {
            ValidationErrors.Add(FString::Printf(TEXT("Invalid type name for material type")));
        }
    }
    
    if (ValidationErrors.Num() > 0)
    {
        for (const FString& Error : ValidationErrors)
        {
            AddErrorMessage(Error);
        }
        return false;
    }
    
    // Register the types
    for (int32 i = 0; i < MaterialTypes.Num(); ++i)
    {
        // Check for cancellation
        if (bCancelled)
        {
            return false;
        }
        
        const FMaterialTypeInfo& TypeInfo = MaterialTypes[i];
        
        // Skip already registered types
        if (Registry.IsMaterialTypeRegistered(TypeInfo.TypeName))
        {
            // Still count as processed for progress tracking
            TypeProgress.ProcessedTypes++;
            UpdateProgress();
            continue;
        }
        
        // Try to register the type
        uint32 TypeId = Registry.RegisterMaterialType(
            TypeInfo.TypeName,
            TypeInfo.Priority);
            
        if (TypeId == 0)
        {
            TypeProgress.FailedTypes++;
            AddErrorMessage(FString::Printf(TEXT("Failed to register material type: %s"), *TypeInfo.TypeName.ToString()));
        }
        else
        {
            // Register material properties if available after type registration
            if (TypeId != 0)
            {
                // Get existing property map for the type
                TMap<FName, TSharedPtr<FMaterialPropertyBase>> PropertyMap = Registry.GetAllMaterialProperties(TypeId);
                for (const auto& PropertyPair : PropertyMap)
                {
                    if (!Registry.RegisterMaterialProperty(TypeId, PropertyPair.Value))
                    {
                        AddErrorMessage(FString::Printf(TEXT("Failed to register property '%s' for material type: %s"), 
                            *PropertyPair.Key.ToString(), *TypeInfo.TypeName.ToString()));
                    }
                }
                
                // Set capabilities if specified
                if (TypeInfo.Capabilities != EMaterialCapabilities::None)
                {
                    Registry.AddMaterialCapability(TypeId, static_cast<EMaterialCapabilities>(TypeInfo.Capabilities));
                }
                
                // Set category if specified
                if (TypeInfo.Category != NAME_None)
                {
                    Registry.SetMaterialCategory(TypeId, TypeInfo.Category);
                }
            }
        }
        
        TypeProgress.ProcessedTypes++;
        UpdateProgress();
        
        // Add small sleep to avoid blocking the thread completely
        FPlatformProcess::Sleep(0.001f);
    }
    
    return TypeProgress.FailedTypes == 0;
}

bool FTypeRegistrationOperation::ExecuteSDFFieldTypeBatchRegistration()
{
    FSDFTypeRegistry& Registry = FSDFTypeRegistry::Get();
    TypeProgress.TotalTypes = SDFFieldTypes.Num();
    
    // Validate all types before registering
    TArray<FString> ValidationErrors;
    for (const FSDFFieldTypeInfo& TypeInfo : SDFFieldTypes)
    {
        // Skip types that are already registered
        if (Registry.IsFieldTypeRegistered(TypeInfo.TypeName))
        {
            continue;
        }
        
        // Basic validation
        if (TypeInfo.TypeName == NAME_None)
        {
            ValidationErrors.Add(FString::Printf(TEXT("Invalid type name for SDF field type")));
        }
    }
    
    if (ValidationErrors.Num() > 0)
    {
        for (const FString& Error : ValidationErrors)
        {
            AddErrorMessage(Error);
        }
        return false;
    }
    
    // Register the types
    for (int32 i = 0; i < SDFFieldTypes.Num(); ++i)
    {
        // Check for cancellation
        if (bCancelled)
        {
            return false;
        }
        
        const FSDFFieldTypeInfo& TypeInfo = SDFFieldTypes[i];
        
        // Skip already registered types
        if (Registry.IsFieldTypeRegistered(TypeInfo.TypeName))
        {
            // Still count as processed for progress tracking
            TypeProgress.ProcessedTypes++;
            UpdateProgress();
            continue;
        }
        
        // Try to register the type
        uint32 TypeId = Registry.RegisterFieldType(
            TypeInfo.TypeName,
            TypeInfo.ChannelCount,
            TypeInfo.AlignmentRequirement,
            TypeInfo.bSupportsGPU);
            
        if (TypeId == 0)
        {
            TypeProgress.FailedTypes++;
            AddErrorMessage(FString::Printf(TEXT("Failed to register SDF field type: %s"), *TypeInfo.TypeName.ToString()));
        }
        
        TypeProgress.ProcessedTypes++;
        UpdateProgress();
        
        // Add small sleep to avoid blocking the thread completely
        FPlatformProcess::Sleep(0.001f);
    }
    
    return TypeProgress.FailedTypes == 0;
}

bool FTypeRegistrationOperation::ExecuteSDFOperationsBatchRegistration()
{
    FSDFTypeRegistry& Registry = FSDFTypeRegistry::Get();
    TypeProgress.TotalTypes = SDFOperations.Num();
    
    // Validate all operations before registering
    TArray<FString> ValidationErrors;
    for (const FSDFOperationInfo& OpInfo : SDFOperations)
    {
        // Skip operations that are already registered
        if (Registry.IsOperationRegistered(OpInfo.OperationName))
        {
            continue;
        }
        
        // Basic validation
        if (OpInfo.OperationName == NAME_None)
        {
            ValidationErrors.Add(FString::Printf(TEXT("Invalid operation name for SDF operation")));
        }
    }
    
    if (ValidationErrors.Num() > 0)
    {
        for (const FString& Error : ValidationErrors)
        {
            AddErrorMessage(Error);
        }
        return false;
    }
    
    // Register the operations
    for (int32 i = 0; i < SDFOperations.Num(); ++i)
    {
        // Check for cancellation
        if (bCancelled)
        {
            return false;
        }
        
        const FSDFOperationInfo& OpInfo = SDFOperations[i];
        
        // Skip already registered operations
        if (Registry.IsOperationRegistered(OpInfo.OperationName))
        {
            // Still count as processed for progress tracking
            TypeProgress.ProcessedTypes++;
            UpdateProgress();
            continue;
        }
        
        // Try to register the operation
        uint32 OperationId = Registry.RegisterOperation(
            OpInfo.OperationName,
            OpInfo.OperationType,
            OpInfo.InputCount,
            OpInfo.bSupportsSmoothing);
            
        if (OperationId == 0)
        {
            TypeProgress.FailedTypes++;
            AddErrorMessage(FString::Printf(TEXT("Failed to register SDF operation: %s"), *OpInfo.OperationName.ToString()));
        }
        
        TypeProgress.ProcessedTypes++;
        UpdateProgress();
        
        // Add small sleep to avoid blocking the thread completely
        FPlatformProcess::Sleep(0.001f);
    }
    
    return TypeProgress.FailedTypes == 0;
}

bool FTypeRegistrationOperation::ExecuteSVONodeTypeBatchRegistration()
{
    FSVOTypeRegistry& Registry = FSVOTypeRegistry::Get();
    TypeProgress.TotalTypes = SVONodeTypes.Num();
    
    // Validate all types before registering
    TArray<FString> ValidationErrors;
    for (const FSVONodeTypeInfo& TypeInfo : SVONodeTypes)
    {
        // Skip types that are already registered
        if (Registry.IsNodeTypeRegistered(TypeInfo.TypeName))
        {
            continue;
        }
        
        // Basic validation
        if (TypeInfo.TypeName == NAME_None)
        {
            ValidationErrors.Add(FString::Printf(TEXT("Invalid type name for SVO node type")));
        }
    }
    
    if (ValidationErrors.Num() > 0)
    {
        for (const FString& Error : ValidationErrors)
        {
            AddErrorMessage(Error);
        }
        return false;
    }
    
    // Register the types
    for (int32 i = 0; i < SVONodeTypes.Num(); ++i)
    {
        // Check for cancellation
        if (bCancelled)
        {
            return false;
        }
        
        const FSVONodeTypeInfo& TypeInfo = SVONodeTypes[i];
        
        // Skip already registered types
        if (Registry.IsNodeTypeRegistered(TypeInfo.TypeName))
        {
            // Still count as processed for progress tracking
            TypeProgress.ProcessedTypes++;
            UpdateProgress();
            continue;
        }
        
        // Try optimistic registration first
        bool bOptimisticSuccess = Registry.TryOptimisticRegisterNodeType(
            TypeInfo.TypeName,
            TypeInfo.NodeClass,
            TypeInfo.DataSize,
            TypeInfo.AlignmentRequirement,
            TypeInfo.bSupportsMaterialRelationships);
            
        if (!bOptimisticSuccess)
        {
            // Fall back to regular registration
            uint32 TypeId = Registry.RegisterNodeType(
                TypeInfo.TypeName,
                TypeInfo.NodeClass,
                TypeInfo.DataSize,
                TypeInfo.AlignmentRequirement,
                TypeInfo.bSupportsMaterialRelationships);
                
            if (TypeId == 0)
            {
                TypeProgress.FailedTypes++;
                AddErrorMessage(FString::Printf(TEXT("Failed to register SVO node type: %s"), *TypeInfo.TypeName.ToString()));
            }
            else if (TypeInfo.CapabilitiesFlags != 0)
            {
                Registry.RegisterCapabilities(TypeId, TypeInfo.CapabilitiesFlags);
            }
        }
        
        TypeProgress.ProcessedTypes++;
        UpdateProgress();
        
        // Add small sleep to avoid blocking the thread completely
        FPlatformProcess::Sleep(0.001f);
    }
    
    return TypeProgress.FailedTypes == 0;
}

bool FTypeRegistrationOperation::ExtractTypesFromSourceAsset()
{
    // This function would normally load and parse the source asset file
    // For this implementation, we'll just set up a simplified version
    
    // Verify the source asset exists
    if (!FPaths::FileExists(SourceAsset))
    {
        AddErrorMessage(FString::Printf(TEXT("Source asset file not found: %s"), *SourceAsset));
        return false;
    }
    
    // Determine file type based on extension
    FString Extension = FPaths::GetExtension(SourceAsset).ToLower();
    
    if (Extension == TEXT("json"))
    {
        // Parse JSON file
        FString JsonContent;
        if (!FFileHelper::LoadFileToString(JsonContent, *SourceAsset))
        {
            AddErrorMessage(FString::Printf(TEXT("Failed to load JSON file: %s"), *SourceAsset));
            return false;
        }
        
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
        {
            AddErrorMessage(FString::Printf(TEXT("Failed to parse JSON file: %s"), *SourceAsset));
            return false;
        }
        
        // Process based on registry type
        switch (RegistryType)
        {
            case ETypeRegistrationRegistry::Zone:
                // Parse zone transaction types from JSON
                // For simplicity, we'll create dummy types for demonstration purposes
                {
                    ZoneTypes.Add(FZoneTransactionTypeInfo());
                    ZoneTypes.Last().TypeName = FName(TEXT("AsyncTestZoneType1"));
                    ZoneTypes.Last().ConcurrencyLevel = ETransactionConcurrency::ReadOnly;
                    ZoneTypes.Last().RetryStrategy = ERetryStrategy::None;
                    ZoneTypes.Last().MaxRetries = 3;
                    ZoneTypes.Last().BaseRetryIntervalMs = 100;
                    ZoneTypes.Last().MaterialChannelId = -1;
                    ZoneTypes.Last().ConflictPriority = 0;
                    ZoneTypes.Last().bRequiresVersionTracking = false;
                    ZoneTypes.Last().bSupportsFastPath = true;
                    ZoneTypes.Last().FastPathThreshold = 0.1f;
                    ZoneTypes.Last().bHasReadValidateWritePattern = false;
                    ZoneTypes.Last().bSupportsThreadSafeAccess = true;
                    ZoneTypes.Last().bSupportsPartialProcessing = false;
                    ZoneTypes.Last().bSupportsIncrementalUpdates = false;
                    ZoneTypes.Last().bLowContention = true;
                    ZoneTypes.Last().bSupportsResultMerging = false;
                    ZoneTypes.Last().bSupportsAsyncProcessing = true;
                    ZoneTypes.Last().SchemaVersion = 1;
                    ZoneTypes.Last().Priority = ETransactionPriority::Normal;
                    
                    ZoneTypes.Add(FZoneTransactionTypeInfo());
                    ZoneTypes.Last().TypeName = FName(TEXT("AsyncTestZoneType2"));
                    ZoneTypes.Last().ConcurrencyLevel = ETransactionConcurrency::Optimistic;
                    ZoneTypes.Last().RetryStrategy = ERetryStrategy::ExponentialBackoff;
                    ZoneTypes.Last().MaxRetries = 5;
                    ZoneTypes.Last().BaseRetryIntervalMs = 50;
                    ZoneTypes.Last().MaterialChannelId = -1;
                    ZoneTypes.Last().ConflictPriority = 10;
                    ZoneTypes.Last().bRequiresVersionTracking = true;
                    ZoneTypes.Last().bSupportsFastPath = true;
                    ZoneTypes.Last().FastPathThreshold = 0.2f;
                    ZoneTypes.Last().bHasReadValidateWritePattern = true;
                    ZoneTypes.Last().bSupportsThreadSafeAccess = true;
                    ZoneTypes.Last().bSupportsPartialProcessing = true;
                    ZoneTypes.Last().bSupportsIncrementalUpdates = true;
                    ZoneTypes.Last().bLowContention = false;
                    ZoneTypes.Last().bSupportsResultMerging = true;
                    ZoneTypes.Last().bSupportsAsyncProcessing = true;
                    ZoneTypes.Last().SchemaVersion = 1;
                    ZoneTypes.Last().Priority = ETransactionPriority::High;
                }
                TypeProgress.TotalTypes = ZoneTypes.Num();
                break;
                
            case ETypeRegistrationRegistry::Material:
                // Parse material types from JSON
                // For simplicity, we'll create dummy types for demonstration purposes
                {
                    MaterialTypes.Add(FMaterialTypeInfo());
                    MaterialTypes.Last().TypeName = FName(TEXT("AsyncTestMaterialType1"));
                    MaterialTypes.Last().Priority = EMaterialPriority::Normal;
                    MaterialTypes.Last().Category = FName(TEXT("Test"));
                    
                    MaterialTypes.Add(FMaterialTypeInfo());
                    MaterialTypes.Last().TypeName = FName(TEXT("AsyncTestMaterialType2"));
                    MaterialTypes.Last().Priority = EMaterialPriority::High;
                    MaterialTypes.Last().Category = FName(TEXT("Test"));
                }
                TypeProgress.TotalTypes = MaterialTypes.Num();
                break;
                
            case ETypeRegistrationRegistry::SDF:
                // Parse SDF field types from JSON
                // For simplicity, we'll create dummy types for demonstration purposes
                {
                    SDFFieldTypes.Add(FSDFFieldTypeInfo());
                    SDFFieldTypes.Last().TypeName = FName(TEXT("AsyncTestSDFFieldType1"));
                    SDFFieldTypes.Last().ChannelCount = 1;
                    SDFFieldTypes.Last().AlignmentRequirement = 16;
                    SDFFieldTypes.Last().bSupportsGPU = false;
                    
                    SDFFieldTypes.Add(FSDFFieldTypeInfo());
                    SDFFieldTypes.Last().TypeName = FName(TEXT("AsyncTestSDFFieldType2"));
                    SDFFieldTypes.Last().ChannelCount = 3;
                    SDFFieldTypes.Last().AlignmentRequirement = 32;
                    SDFFieldTypes.Last().bSupportsGPU = true;
                }
                TypeProgress.TotalTypes = SDFFieldTypes.Num();
                break;
                
            case ETypeRegistrationRegistry::SVO:
                // Parse SVO node types from JSON
                // For simplicity, we'll create dummy types for demonstration purposes
                {
                    SVONodeTypes.Add(FSVONodeTypeInfo());
                    SVONodeTypes.Last().TypeName = FName(TEXT("AsyncTestSVONodeType1"));
                    SVONodeTypes.Last().NodeClass = ESVONodeClass::Homogeneous;
                    SVONodeTypes.Last().DataSize = 32;
                    SVONodeTypes.Last().AlignmentRequirement = 16;
                    SVONodeTypes.Last().bSupportsMaterialRelationships = false;
                    
                    SVONodeTypes.Add(FSVONodeTypeInfo());
                    SVONodeTypes.Last().TypeName = FName(TEXT("AsyncTestSVONodeType2"));
                    SVONodeTypes.Last().NodeClass = ESVONodeClass::Interface;
                    SVONodeTypes.Last().DataSize = 64;
                    SVONodeTypes.Last().AlignmentRequirement = 32;
                    SVONodeTypes.Last().bSupportsMaterialRelationships = true;
                }
                TypeProgress.TotalTypes = SVONodeTypes.Num();
                break;
        }
    }
    else
    {
        AddErrorMessage(FString::Printf(TEXT("Unsupported file extension: %s"), *Extension));
        return false;
    }
    
    return true;
}

void FTypeRegistrationOperation::UpdateProgress()
{
    // Create progress info
    FAsyncProgress ProgressUpdate;
    ProgressUpdate.CompletionPercentage = TypeProgress.TotalTypes > 0 
        ? static_cast<float>(TypeProgress.ProcessedTypes) / TypeProgress.TotalTypes
        : 0.0f;
    
    ProgressUpdate.CurrentStage = 1;
    ProgressUpdate.TotalStages = 1;
    ProgressUpdate.ItemsProcessed = TypeProgress.ProcessedTypes;
    ProgressUpdate.TotalItems = TypeProgress.TotalTypes;
    
    // Calculate elapsed time
    double CurrentTime = FPlatformTime::Seconds();
    ProgressUpdate.ElapsedTimeSeconds = CurrentTime - GetStartTime();
    
    // Estimate remaining time
    if (ProgressUpdate.CompletionPercentage > 0.0f)
    {
        ProgressUpdate.EstimatedTimeRemainingSeconds = (ProgressUpdate.ElapsedTimeSeconds / ProgressUpdate.CompletionPercentage) 
            * (1.0f - ProgressUpdate.CompletionPercentage);
    }
    
    // Format status message
    ProgressUpdate.StatusMessage = FString::Printf(TEXT("Registering types: %d/%d (%.1f%%) - %d failed"),
        TypeProgress.ProcessedTypes,
        TypeProgress.TotalTypes,
        ProgressUpdate.CompletionPercentage * 100.0f,
        TypeProgress.FailedTypes);
    
    // Update the operation progress
    UpdateProgress(ProgressUpdate);
}

void FTypeRegistrationOperation::AddErrorMessage(const FString& ErrorMessage)
{
    TypeProgress.ErrorMessages.Add(ErrorMessage);
}

FString FTypeRegistrationOperation::GetOperationTypeForRegistry(ETypeRegistrationRegistry RegType)
{
    switch (RegType)
    {
        case ETypeRegistrationRegistry::Zone:
            return ZoneTypeRegistrationOperationType;
        case ETypeRegistrationRegistry::Material:
            return MaterialTypeRegistrationOperationType;
        case ETypeRegistrationRegistry::SDF:
            return SDFFieldTypeRegistrationOperationType;
        case ETypeRegistrationRegistry::SVO:
            return SVONodeTypeRegistrationOperationType;
        default:
            return TEXT("UnknownTypeRegistration");
    }
}

// ====================================================================================================
// FTypeRegistrationOperationFactory Implementation
// ====================================================================================================

void FTypeRegistrationOperationFactory::Initialize()
{
    IAsyncOperation& AsyncInterface = IAsyncOperation::Get();
    FAsyncTaskManager& AsyncManager = static_cast<FAsyncTaskManager&>(AsyncInterface);
    
    // Register operation types with the AsyncTaskManager
    AsyncManager.RegisterOperationType(ZoneTypeRegistrationOperationType, 
        [](uint64 Id, const FString& Name) { return CreateZoneTypeRegistration(Id, Name); });
        
    AsyncManager.RegisterOperationType(MaterialTypeRegistrationOperationType, 
        [](uint64 Id, const FString& Name) { return CreateMaterialTypeRegistration(Id, Name); });
        
    AsyncManager.RegisterOperationType(SDFFieldTypeRegistrationOperationType, 
        [](uint64 Id, const FString& Name) { return CreateSDFFieldTypeRegistration(Id, Name); });
        
    AsyncManager.RegisterOperationType(SVONodeTypeRegistrationOperationType, 
        [](uint64 Id, const FString& Name) { return CreateSVONodeTypeRegistration(Id, Name); });
}

void FTypeRegistrationOperationFactory::Shutdown()
{
    // Nothing to do here for now
}

FTypeRegistrationOperation* FTypeRegistrationOperationFactory::CreateZoneTypeRegistration(uint64 Id, const FString& Name)
{
    return new FTypeRegistrationOperation(Id, Name, ETypeRegistrationRegistry::Zone, TEXT(""));
}

FTypeRegistrationOperation* FTypeRegistrationOperationFactory::CreateMaterialTypeRegistration(uint64 Id, const FString& Name)
{
    return new FTypeRegistrationOperation(Id, Name, ETypeRegistrationRegistry::Material, TEXT(""));
}

FTypeRegistrationOperation* FTypeRegistrationOperationFactory::CreateSDFFieldTypeRegistration(uint64 Id, const FString& Name)
{
    return new FTypeRegistrationOperation(Id, Name, ETypeRegistrationRegistry::SDF, TEXT(""));
}

FTypeRegistrationOperation* FTypeRegistrationOperationFactory::CreateSVONodeTypeRegistration(uint64 Id, const FString& Name)
{
    return new FTypeRegistrationOperation(Id, Name, ETypeRegistrationRegistry::SVO, TEXT(""));
}

void FTypeRegistrationOperation::UpdateProgress(const FAsyncProgress& InProgress)
{
    // Call the base implementation from FAsyncOperationImpl
    FAsyncOperationImpl::UpdateProgress(InProgress);
}

void FTypeRegistrationOperation::SetProgress(const FAsyncProgress& InProgress)
{
    // Call the base implementation from FAsyncOperationImpl
    UpdateProgress(InProgress);
} 