// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"
#include "../Public/ZoneTypeRegistry.h"
#include "../Public/MaterialRegistry.h"
#include "../Public/SDFTypeRegistry.h"
#include "../Public/SVOTypeRegistry.h"

/**
 * Enum defining the registry type for a registration operation
 */
enum class ETypeRegistrationRegistry : uint8
{
    Zone,
    Material,
    SDF,
    SVO
};

/**
 * Structure containing progress information for a type registration operation
 */
struct FTypeRegistrationProgress
{
    /** Total number of types to register */
    int32 TotalTypes;
    
    /** Number of types processed so far */
    int32 ProcessedTypes;
    
    /** Number of types that failed to register */
    int32 FailedTypes;
    
    /** Error messages from failed registrations */
    TArray<FString> ErrorMessages;
    
    /** Default constructor */
    FTypeRegistrationProgress()
        : TotalTypes(0)
        , ProcessedTypes(0)
        , FailedTypes(0)
    {
    }
};

/**
 * Async operation implementation for type registration
 * Handles loading and registering types from assets or batch data
 */
class FTypeRegistrationOperation : public FAsyncOperationImpl
{
public:
    /** Constructor for source asset registration */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, ETypeRegistrationRegistry InRegistryType, const FString& InSourceAsset);
    
    /** Constructor for batch registration of zone transaction types */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FZoneTransactionTypeInfo>& InTypes);
    
    /** Constructor for batch registration of material types */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FMaterialTypeInfo>& InTypes);
    
    /** Constructor for batch registration of SDF field types */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSDFFieldTypeInfo>& InTypes);
    
    /** Constructor for batch registration of SDF operations */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSDFOperationInfo>& InOperations);
    
    /** Constructor for batch registration of SVO node types */
    FTypeRegistrationOperation(uint64 InId, const FString& InName, const TArray<FSVONodeTypeInfo>& InTypes);
    
    /** Destructor */
    virtual ~FTypeRegistrationOperation();
    
    //~ Begin FAsyncOperationImpl interface
    virtual bool Execute() override;
    virtual bool Cancel() override;
    //~ End FAsyncOperationImpl interface

    /** Helper method to get the operation type string for a registry type */
    FString GetOperationTypeForRegistry(ETypeRegistrationRegistry RegType);

    /** Updates progress information during registration */
    void UpdateProgress();
    
    /** Updates progress with the given progress info */
    void UpdateProgress(const FAsyncProgress& InProgress);

    /**
     * Sets the current progress for this operation
     * @param InProgress The progress information
     */
    void SetProgress(const FAsyncProgress& InProgress);

private:
    /** Executes a source asset registration operation */
    bool ExecuteSourceAssetRegistration();
    
    /** Executes a batch registration of zone transaction types */
    bool ExecuteZoneTypeBatchRegistration();
    
    /** Executes a batch registration of material types */
    bool ExecuteMaterialTypeBatchRegistration();
    
    /** Executes a batch registration of SDF field types */
    bool ExecuteSDFFieldTypeBatchRegistration();
    
    /** Executes a batch registration of SDF operations */
    bool ExecuteSDFOperationsBatchRegistration();
    
    /** Executes a batch registration of SVO node types */
    bool ExecuteSVONodeTypeBatchRegistration();
    
    /** Extracts types from a source asset */
    bool ExtractTypesFromSourceAsset();
    
    /** Adds an error message to the progress information */
    void AddErrorMessage(const FString& ErrorMessage);
    
    /** Registry type for this operation */
    ETypeRegistrationRegistry RegistryType;
    
    /** Source asset path for asset-based registration */
    FString SourceAsset;
    
    /** Zone transaction types for batch registration */
    TArray<FZoneTransactionTypeInfo> ZoneTypes;
    
    /** Material types for batch registration */
    TArray<FMaterialTypeInfo> MaterialTypes;
    
    /** SDF field types for batch registration */
    TArray<FSDFFieldTypeInfo> SDFFieldTypes;
    
    /** SDF operations for batch registration */
    TArray<FSDFOperationInfo> SDFOperations;
    
    /** SVO node types for batch registration */
    TArray<FSVONodeTypeInfo> SVONodeTypes;
    
    /** Progress information for this operation */
    FTypeRegistrationProgress TypeProgress;
    
    /** Whether this operation is using a source asset */
    bool bUsingSourceAsset;
    
    /** Whether the operation has been cancelled */
    bool bCancelled;
};

/**
 * Factory for creating type registration operations
 * Registers with the AsyncTaskManager to create operations of various types
 */
class FTypeRegistrationOperationFactory
{
public:
    /** Initialize the factory and register with AsyncTaskManager */
    static void Initialize();
    
    /** Shutdown the factory */
    static void Shutdown();
    
    /** Create a zone type registration operation */
    static FTypeRegistrationOperation* CreateZoneTypeRegistration(uint64 Id, const FString& Name);
    
    /** Create a material type registration operation */
    static FTypeRegistrationOperation* CreateMaterialTypeRegistration(uint64 Id, const FString& Name);
    
    /** Create an SDF field type registration operation */
    static FTypeRegistrationOperation* CreateSDFFieldTypeRegistration(uint64 Id, const FString& Name);
    
    /** Create an SVO node type registration operation */
    static FTypeRegistrationOperation* CreateSVONodeTypeRegistration(uint64 Id, const FString& Name);
}; 