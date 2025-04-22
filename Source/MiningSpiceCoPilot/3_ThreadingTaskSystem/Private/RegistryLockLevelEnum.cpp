#include "RegistryLockLevelEnum.h"
#include "CoreMinimal.h"
#include "ThreadSafety.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/EnumProperty.h"

// Template specialization for ERegistryLockLevel
template<>
UEnum* StaticEnum<ERegistryLockLevel>()
{
    static UEnum* RegisteredEnum = nullptr;
    
    if (RegisteredEnum == nullptr)
    {
        // Create a package for this enum
        UPackage* Package = CreatePackage(TEXT("/Script/MiningSpiceCoPilot"));
        
        // Create a new UEnum instance
        RegisteredEnum = NewObject<UEnum>(Package, TEXT("ERegistryLockLevel"), RF_Public | RF_Standalone);
        
        // Create the array of enum entries using the correct format
        TArray<TPair<FName, int64>> EnumNames;
        EnumNames.Add(TPair<FName, int64>(FName("Service"), static_cast<int64>(ERegistryLockLevel::Service)));
        EnumNames.Add(TPair<FName, int64>(FName("Zone"), static_cast<int64>(ERegistryLockLevel::Zone)));
        EnumNames.Add(TPair<FName, int64>(FName("Material"), static_cast<int64>(ERegistryLockLevel::Material)));
        EnumNames.Add(TPair<FName, int64>(FName("SVO"), static_cast<int64>(ERegistryLockLevel::SVO)));
        EnumNames.Add(TPair<FName, int64>(FName("SDF"), static_cast<int64>(ERegistryLockLevel::SDF)));

        // Set the enum values using the correct method signature
        RegisteredEnum->SetEnums(EnumNames, UEnum::ECppForm::EnumClass);
    }
    
    return RegisteredEnum;
} 