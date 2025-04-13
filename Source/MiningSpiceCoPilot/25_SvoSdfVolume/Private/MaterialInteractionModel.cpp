// MaterialInteractionModel.cpp
// Material interaction modeling with Boolean operations and transitions

#include "25_SvoSdfVolume/Public/MaterialInteractionModel.h"
#include "25_SvoSdfVolume/Public/MaterialSDFManager.h"
#include "1_Core/Public/ServiceLocator.h"
#include "5_Config/Public/IConfigManager.h"
#include "53_MaterialProperties/Public/MaterialPropertyManager.h"
#include "4_Events/Public/EventBus.h"
#include "HAL/PlatformMisc.h"
#include "Math/UnrealMathUtility.h"

FMaterialInteractionModel::FMaterialInteractionModel()
    : MaterialManager(nullptr)
    , DefaultBlendType(EBlendType::Linear)
    , DefaultPriority(50)
    , bNetworkSynchronized(true)
{
    // Initialize with default rules
    InitializeDefaultRules();
}

FMaterialInteractionModel::~FMaterialInteractionModel()
{
    // Clean up any resources
    MaterialRules.Empty();
    BlendFunctions.Empty();
}

void FMaterialInteractionModel::Initialize(FMaterialSDFManager* InMaterialManager)
{
    MaterialManager = InMaterialManager;
    
    // Load configuration from System 5
    auto* ConfigManager = IServiceLocator::Get().ResolveService<IConfigManager>();
    if (ConfigManager)
    {
        DefaultBlendType = (EBlendType)ConfigManager->GetValue<int32>("MaterialInteraction.DefaultBlendType", (int32)EBlendType::Linear);
        DefaultPriority = ConfigManager->GetValue<uint8>("MaterialInteraction.DefaultPriority", 50);
        bNetworkSynchronized = ConfigManager->GetValue<bool>("MaterialInteraction.NetworkSynchronized", true);
    }
    
    // Register standard blend functions
    RegisterBlendFunctions();
    
    // Load material-specific rules from Material Property System (53)
    auto* MaterialPropertyManager = IServiceLocator::Get().ResolveService<IMaterialPropertyManager>();
    if (MaterialPropertyManager)
    {
        LoadMaterialRulesFromProperties(MaterialPropertyManager);
    }
    
    // Subscribe to material property changes through System 4: Event System
    auto* EventBus = IServiceLocator::Get().ResolveService<IEventBus>();
    if (EventBus)
    {
        EventBus->SubscribeToEvent("MaterialPropertyChanged", [this](const FEventData& EventData) {
            OnMaterialPropertyChanged(EventData);
        });
    }
}

void FMaterialInteractionModel::SetBlendType(uint8 MaterialA, uint8 MaterialB, EBlendType BlendType)
{
    FMaterialPair Key(FMath::Min(MaterialA, MaterialB), FMath::Max(MaterialA, MaterialB));
    
    // Check if rule exists
    if (MaterialRules.Contains(Key))
    {
        MaterialRules[Key].BlendType = BlendType;
    }
    else
    {
        // Create new rule with default values
        FMaterialInteractionRule Rule;
        Rule.BlendType = BlendType;
        Rule.Priority = DefaultPriority;
        Rule.InteractionBehavior = EInteractionBehavior::Blend;
        MaterialRules.Add(Key, Rule);
    }
    
    // Network state tracking
    if (bNetworkSynchronized)
    {
        MarkRuleAsModified(Key);
    }
    
    // Publish event about rule change
    auto* EventBus = IServiceLocator::Get().ResolveService<IEventBus>();
    if (EventBus)
    {
        FEventData EventData;
        EventData.Add("MaterialA", MaterialA);
        EventData.Add("MaterialB", MaterialB);
        EventData.Add("BlendType", (int32)BlendType);
        EventBus->PublishEvent("MaterialInteractionRuleChanged", EventData);
    }
}

void FMaterialInteractionModel::SetInteractionBehavior(uint8 MaterialA, uint8 MaterialB, EInteractionBehavior Behavior)
{
    FMaterialPair Key(FMath::Min(MaterialA, MaterialB), FMath::Max(MaterialA, MaterialB));
    
    // Check if rule exists and update or create
    if (MaterialRules.Contains(Key))
    {
        MaterialRules[Key].InteractionBehavior = Behavior;
    }
    else
    {
        // Create new rule with default values
        FMaterialInteractionRule Rule;
        Rule.BlendType = DefaultBlendType;
        Rule.Priority = DefaultPriority;
        Rule.InteractionBehavior = Behavior;
        MaterialRules.Add(Key, Rule);
    }
    
    // Network state tracking
    if (bNetworkSynchronized)
    {
        MarkRuleAsModified(Key);
    }
    
    // Publish event about rule change
    auto* EventBus = IServiceLocator::Get().ResolveService<IEventBus>();
    if (EventBus)
    {
        FEventData EventData;
        EventData.Add("MaterialA", MaterialA);
        EventData.Add("MaterialB", MaterialB);
        EventData.Add("Behavior", (int32)Behavior);
        EventBus->PublishEvent("MaterialInteractionRuleChanged", EventData);
    }
}

void FMaterialInteractionModel::SetMaterialPriority(uint8 MaterialIndex, uint8 Priority)
{
    // Store in material-specific priorities
    MaterialPriorities.Add(MaterialIndex, Priority);
    
    // Update all interaction rules involving this material
    for (auto& RulePair : MaterialRules)
    {
        if (RulePair.Key.MaterialA == MaterialIndex || RulePair.Key.MaterialB == MaterialIndex)
        {
            // Mark rule as modified for network synchronization
            if (bNetworkSynchronized)
            {
                MarkRuleAsModified(RulePair.Key);
            }
        }
    }
    
    // Publish event about priority change
    auto* EventBus = IServiceLocator::Get().ResolveService<IEventBus>();
    if (EventBus)
    {
        FEventData EventData;
        EventData.Add("MaterialIndex", MaterialIndex);
        EventData.Add("Priority", Priority);
        EventBus->PublishEvent("MaterialPriorityChanged", EventData);
    }
}

EBlendType FMaterialInteractionModel::GetBlendType(uint8 MaterialA, uint8 MaterialB) const
{
    FMaterialPair Key(FMath::Min(MaterialA, MaterialB), FMath::Max(MaterialA, MaterialB));
    
    const FMaterialInteractionRule* Rule = MaterialRules.Find(Key);
    return Rule ? Rule->BlendType : DefaultBlendType;
}

EInteractionBehavior FMaterialInteractionModel::GetInteractionBehavior(uint8 MaterialA, uint8 MaterialB) const
{
    FMaterialPair Key(FMath::Min(MaterialA, MaterialB), FMath::Max(MaterialA, MaterialB));
    
    const FMaterialInteractionRule* Rule = MaterialRules.Find(Key);
    return Rule ? Rule->InteractionBehavior : EInteractionBehavior::Blend;
}

uint8 FMaterialInteractionModel::GetMaterialPriority(uint8 MaterialIndex) const
{
    const uint8* Priority = MaterialPriorities.Find(MaterialIndex);
    return Priority ? *Priority : DefaultPriority;
}

float FMaterialInteractionModel::BlendMaterials(uint8 MaterialA, uint8 MaterialB, float Alpha) const
{
    EBlendType BlendType = GetBlendType(MaterialA, MaterialB);
    return ApplyBlendFunction(BlendType, Alpha);
}

uint8 FMaterialInteractionModel::ResolvePriorityWinner(uint8 MaterialA, uint8 MaterialB) const
{
    uint8 PriorityA = GetMaterialPriority(MaterialA);
    uint8 PriorityB = GetMaterialPriority(MaterialB);
    
    if (PriorityA > PriorityB)
    {
        return MaterialA;
    }
    else if (PriorityB > PriorityA)
    {
        return MaterialB;
    }
    
    // If priorities are equal, choose the lower index (deterministic choice)
    return FMath::Min(MaterialA, MaterialB);
}

float FMaterialInteractionModel::ApplyBooleanOperation(const TArray<float>& MaterialDistances, uint8 TargetMaterial, EBooleanOperation Operation) const
{
    if (!MaterialManager)
    {
        return 0.0f;
    }
    
    // Get the target material's distance
    float TargetDistance = MaterialDistances.IsValidIndex(TargetMaterial) ? MaterialDistances[TargetMaterial] : MAX_FLT;
    float Result = TargetDistance;
    
    // Apply the boolean operation
    switch (Operation)
    {
        case EBooleanOperation::Union:
            for (int32 i = 0; i < MaterialDistances.Num(); ++i)
            {
                if (i != TargetMaterial)
                {
                    Result = FMath::Min(Result, MaterialDistances[i]);
                }
            }
            break;
            
        case EBooleanOperation::Subtraction:
            for (int32 i = 0; i < MaterialDistances.Num(); ++i)
            {
                if (i != TargetMaterial)
                {
                    Result = FMath::Max(Result, -MaterialDistances[i]);
                }
            }
            break;
            
        case EBooleanOperation::Intersection:
            for (int32 i = 0; i < MaterialDistances.Num(); ++i)
            {
                if (i != TargetMaterial)
                {
                    Result = FMath::Max(Result, MaterialDistances[i]);
                }
            }
            break;
            
        case EBooleanOperation::SmoothUnion:
            for (int32 i = 0; i < MaterialDistances.Num(); ++i)
            {
                if (i != TargetMaterial)
                {
                    float k = 0.1f; // Smoothing factor - could be customizable
                    float h = FMath::Max(k - FMath::Abs(Result - MaterialDistances[i]), 0.0f) / k;
                    Result = FMath::Min(Result, MaterialDistances[i]) - h * h * h * k * (1.0f / 6.0f);
                }
            }
            break;
    }
    
    return Result;
}

void FMaterialInteractionModel::RegisterCustomBlendFunction(EBlendType BlendType, FBlendFunction BlendFunction)
{
    BlendFunctions.Add(BlendType, BlendFunction);
}

float FMaterialInteractionModel::ApplyBlendFunction(EBlendType BlendType, float Alpha) const
{
    // Clamp alpha to [0,1]
    float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    
    // Check for custom blend function
    FBlendFunction* CustomFunction = BlendFunctions.Find(BlendType);
    if (CustomFunction)
    {
        return (*CustomFunction)(ClampedAlpha);
    }
    
    // Apply standard blend functions
    switch (BlendType)
    {
        case EBlendType::Linear:
            return ClampedAlpha;
            
        case EBlendType::Smoothstep:
            return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
            
        case EBlendType::Exponential:
            return ClampedAlpha * ClampedAlpha;
            
        case EBlendType::Sinusoidal:
            return (FMath::Sin((ClampedAlpha - 0.5f) * PI) + 1.0f) * 0.5f;
            
        case EBlendType::Step:
            return (ClampedAlpha >= 0.5f) ? 1.0f : 0.0f;
            
        case EBlendType::SmoothUnion:
        {
            float k = 0.1f; // Smoothing factor
            float h = FMath::Max(k - FMath::Abs(ClampedAlpha - 0.5f) * 2.0f, 0.0f) / k;
            return FMath::Min(ClampedAlpha, 1.0f - ClampedAlpha) - h * h * k * 0.25f;
        }
            
        default:
            return ClampedAlpha;
    }
}

void FMaterialInteractionModel::InitializeDefaultRules()
{
    // Create default rule
    FMaterialInteractionRule DefaultRule;
    DefaultRule.BlendType = DefaultBlendType;
    DefaultRule.Priority = DefaultPriority;
    DefaultRule.InteractionBehavior = EInteractionBehavior::Blend;
    
    // Add some standard material rules
    // This would be expanded based on game-specific materials
    // For now, just a few examples
    
    // Rock vs. Dirt - smooth blend
    FMaterialPair RockDirt(1, 2); // Assuming 1=Rock, 2=Dirt
    FMaterialInteractionRule RockDirtRule;
    RockDirtRule.BlendType = EBlendType::Smoothstep;
    RockDirtRule.Priority = 60;
    RockDirtRule.InteractionBehavior = EInteractionBehavior::Blend;
    MaterialRules.Add(RockDirt, RockDirtRule);
    
    // Metal vs. Rock - sharp boundary
    FMaterialPair MetalRock(0, 1); // Assuming 0=Metal, 1=Rock
    FMaterialInteractionRule MetalRockRule;
    MetalRockRule.BlendType = EBlendType::Step;
    MetalRockRule.Priority = 70;
    MetalRockRule.InteractionBehavior = EInteractionBehavior::Boundary;
    MaterialRules.Add(MetalRock, MetalRockRule);
    
    // Set default material priorities
    MaterialPriorities.Add(0, 90); // Metal (highest priority)
    MaterialPriorities.Add(1, 70); // Rock
    MaterialPriorities.Add(2, 50); // Dirt
    MaterialPriorities.Add(3, 30); // Sand
}

void FMaterialInteractionModel::RegisterBlendFunctions()
{
    // Register standard blend functions
    BlendFunctions.Add(EBlendType::Linear, [](float Alpha) { return Alpha; });
    
    BlendFunctions.Add(EBlendType::Smoothstep, [](float Alpha) {
        return Alpha * Alpha * (3.0f - 2.0f * Alpha);
    });
    
    BlendFunctions.Add(EBlendType::Exponential, [](float Alpha) {
        return Alpha * Alpha;
    });
    
    BlendFunctions.Add(EBlendType::Sinusoidal, [](float Alpha) {
        return (FMath::Sin((Alpha - 0.5f) * PI) + 1.0f) * 0.5f;
    });
    
    BlendFunctions.Add(EBlendType::Step, [](float Alpha) {
        return (Alpha >= 0.5f) ? 1.0f : 0.0f;
    });
    
    BlendFunctions.Add(EBlendType::SmoothUnion, [](float Alpha) {
        float k = 0.1f; // Smoothing factor
        float h = FMath::Max(k - FMath::Abs(Alpha - 0.5f) * 2.0f, 0.0f) / k;
        return FMath::Min(Alpha, 1.0f - Alpha) - h * h * k * 0.25f;
    });
    
    // Add cubic ease in/out blend
    BlendFunctions.Add(EBlendType::CubicEase, [](float Alpha) {
        return Alpha < 0.5f
            ? 4.0f * Alpha * Alpha * Alpha
            : 1.0f - FMath::Pow(-2.0f * Alpha + 2.0f, 3.0f) * 0.5f;
    });
}

void FMaterialInteractionModel::LoadMaterialRulesFromProperties(IMaterialPropertyManager* PropertyManager)
{
    if (!PropertyManager)
    {
        return;
    }
    
    // Get number of materials from property manager
    int32 MaterialCount = PropertyManager->GetMaterialCount();
    
    // Load priorities
    for (int32 i = 0; i < MaterialCount; ++i)
    {
        uint8 MaterialIndex = (uint8)i;
        uint8 Priority = PropertyManager->GetMaterialPropertyValue<uint8>(MaterialIndex, "Priority", DefaultPriority);
        MaterialPriorities.Add(MaterialIndex, Priority);
    }
    
    // Load interaction rules
    for (int32 i = 0; i < MaterialCount; ++i)
    {
        for (int32 j = i; j < MaterialCount; ++j)
        {
            uint8 MaterialA = (uint8)i;
            uint8 MaterialB = (uint8)j;
            
            // Only create rules where explicitly defined in properties
            FString RuleKey = FString::Printf(TEXT("Interaction_%d_%d"), MaterialA, MaterialB);
            if (PropertyManager->HasMaterialPropertyGroup(RuleKey))
            {
                FMaterialPair Key(MaterialA, MaterialB);
                FMaterialInteractionRule Rule;
                
                int32 BlendTypeValue = PropertyManager->GetMaterialPropertyValue<int32>(RuleKey, "BlendType", (int32)DefaultBlendType);
                Rule.BlendType = (EBlendType)BlendTypeValue;
                
                int32 BehaviorValue = PropertyManager->GetMaterialPropertyValue<int32>(RuleKey, "InteractionBehavior", (int32)EInteractionBehavior::Blend);
                Rule.InteractionBehavior = (EInteractionBehavior)BehaviorValue;
                
                MaterialRules.Add(Key, Rule);
            }
        }
    }
}

void FMaterialInteractionModel::OnMaterialPropertyChanged(const FEventData& EventData)
{
    // Check if the event relates to material properties we care about
    if (EventData.Contains("MaterialIndex") && EventData.Contains("PropertyName"))
    {
        uint8 MaterialIndex = (uint8)EventData.Get<int32>("MaterialIndex");
        FString PropertyName = EventData.Get<FString>("PropertyName");
        
        if (PropertyName == "Priority")
        {
            // Update material priority
            uint8 Priority = (uint8)EventData.Get<int32>("NewValue");
            SetMaterialPriority(MaterialIndex, Priority);
        }
        else if (PropertyName.StartsWith("Interaction_"))
        {
            // Parse material indices from property name (format: "Interaction_A_B")
            TArray<FString> Parts;
            PropertyName.ParseIntoArray(Parts, TEXT("_"));
            
            if (Parts.Num() >= 3)
            {
                uint8 MaterialA = FCString::Atoi(*Parts[1]);
                uint8 MaterialB = FCString::Atoi(*Parts[2]);
                
                // Request full reload of interaction rules
                auto* PropertyManager = IServiceLocator::Get().ResolveService<IMaterialPropertyManager>();
                if (PropertyManager)
                {
                    LoadMaterialRulesFromProperties(PropertyManager);
                }
            }
        }
    }
}

void FMaterialInteractionModel::MarkRuleAsModified(const FMaterialPair& MaterialPair)
{
    // Track rule modification for network sync
    ModifiedRules.Add(MaterialPair);
    
    // Update version counter for network state tracking
    CurrentVersionCounter.IncrementExchange();
}

TArray<FMaterialPair> FMaterialInteractionModel::GetRulesModifiedSince(uint64 BaseVersion) const
{
    TArray<FMaterialPair> Result;
    
    // Return all rules modified since specified version
    for (const auto& ModifiedRule : ModifiedRules)
    {
        // Implementation would track rule version and check against BaseVersion
        // For now, just return all modified rules
        Result.Add(ModifiedRule);
    }
    
    return Result;
}

bool FMaterialInteractionModel::SerializeRules(TArray<uint8>& OutData) const
{
    // Would implement full serialization of all material rules
    // For efficient network synchronization
    OutData.Empty();
    
    // Rule count
    int32 RuleCount = MaterialRules.Num();
    OutData.AddUninitialized(sizeof(int32));
    FMemory::Memcpy(OutData.GetData(), &RuleCount, sizeof(int32));
    
    // Serialize each rule
    for (const auto& Pair : MaterialRules)
    {
        // Material pair
        OutData.AddUninitialized(sizeof(FMaterialPair));
        FMemory::Memcpy(OutData.GetData() + OutData.Num() - sizeof(FMaterialPair), &Pair.Key, sizeof(FMaterialPair));
        
        // Rule data
        OutData.AddUninitialized(sizeof(FMaterialInteractionRule));
        FMemory::Memcpy(OutData.GetData() + OutData.Num() - sizeof(FMaterialInteractionRule), &Pair.Value, sizeof(FMaterialInteractionRule));
    }
    
    // Material priorities count
    int32 PriorityCount = MaterialPriorities.Num();
    OutData.AddUninitialized(sizeof(int32));
    FMemory::Memcpy(OutData.GetData() + OutData.Num() - sizeof(int32), &PriorityCount, sizeof(int32));
    
    // Serialize each priority
    for (const auto& Pair : MaterialPriorities)
    {
        // Material index
        OutData.AddUninitialized(sizeof(uint8));
        FMemory::Memcpy(OutData.GetData() + OutData.Num() - sizeof(uint8), &Pair.Key, sizeof(uint8));
        
        // Priority
        OutData.AddUninitialized(sizeof(uint8));
        FMemory::Memcpy(OutData.GetData() + OutData.Num() - sizeof(uint8), &Pair.Value, sizeof(uint8));
    }
    
    return true;
}

bool FMaterialInteractionModel::DeserializeRules(const TArray<uint8>& Data)
{
    // Clear existing rules
    MaterialRules.Empty();
    MaterialPriorities.Empty();
    
    if (Data.Num() < sizeof(int32))
    {
        return false;
    }
    
    int32 Offset = 0;
    
    // Rule count
    int32 RuleCount = 0;
    FMemory::Memcpy(&RuleCount, Data.GetData() + Offset, sizeof(int32));
    Offset += sizeof(int32);
    
    // Deserialize each rule
    for (int32 i = 0; i < RuleCount; ++i)
    {
        if (Offset + sizeof(FMaterialPair) + sizeof(FMaterialInteractionRule) > Data.Num())
        {
            return false;
        }
        
        // Material pair
        FMaterialPair Key;
        FMemory::Memcpy(&Key, Data.GetData() + Offset, sizeof(FMaterialPair));
        Offset += sizeof(FMaterialPair);
        
        // Rule data
        FMaterialInteractionRule Rule;
        FMemory::Memcpy(&Rule, Data.GetData() + Offset, sizeof(FMaterialInteractionRule));
        Offset += sizeof(FMaterialInteractionRule);
        
        // Add rule
        MaterialRules.Add(Key, Rule);
    }
    
    // Check if there's priority data
    if (Offset + sizeof(int32) <= Data.Num())
    {
        // Priority count
        int32 PriorityCount = 0;
        FMemory::Memcpy(&PriorityCount, Data.GetData() + Offset, sizeof(int32));
        Offset += sizeof(int32);
        
        // Deserialize each priority
        for (int32 i = 0; i < PriorityCount; ++i)
        {
            if (Offset + sizeof(uint8) + sizeof(uint8) > Data.Num())
            {
                return false;
            }
            
            // Material index
            uint8 MaterialIndex = 0;
            FMemory::Memcpy(&MaterialIndex, Data.GetData() + Offset, sizeof(uint8));
            Offset += sizeof(uint8);
            
            // Priority
            uint8 Priority = 0;
            FMemory::Memcpy(&Priority, Data.GetData() + Offset, sizeof(uint8));
            Offset += sizeof(uint8);
            
            // Add priority
            MaterialPriorities.Add(MaterialIndex, Priority);
        }
    }
    
    // Clear modification tracking since we just loaded a fresh state
    ModifiedRules.Empty();
    
    return true;
}