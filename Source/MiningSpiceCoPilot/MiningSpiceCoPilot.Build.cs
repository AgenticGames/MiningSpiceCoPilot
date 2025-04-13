// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MiningSpiceCoPilot : ModuleRules
{
	public MiningSpiceCoPilot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		// Add public include paths for all our systems
		PublicIncludePaths.AddRange(new string[] {
			// Core systems
			Path.Combine(ModuleDirectory, "1_CoreRegistry/Public"),
			Path.Combine(ModuleDirectory, "2_MemoryManagement/Public"),
			Path.Combine(ModuleDirectory, "3_ThreadingTaskSystem/Public"),
			Path.Combine(ModuleDirectory, "4_EventSystem/Public"),
			Path.Combine(ModuleDirectory, "5_ConfigurationManagement/Public"),
			Path.Combine(ModuleDirectory, "6_ServiceDependencyRegistry/Public"),
			Path.Combine(ModuleDirectory, "7_FactorySystem/Public"),
			
			// Extended systems
			Path.Combine(ModuleDirectory, "25_SvoSdfVolume/Public"),
			Path.Combine(ModuleDirectory, "2.1_TieredCompression/Public"),
			Path.Combine(ModuleDirectory, "2.2_RegionHibernation/Public"),
			Path.Combine(ModuleDirectory, "2.3_ZoneBasedConcurrentMining/Public"),
			
			// Interface paths - make sure interfaces can be found
			Path.Combine(ModuleDirectory, "Interfaces")
		});
		
		// Add private include paths
		PrivateIncludePaths.AddRange(new string[] {
			// Core systems
			Path.Combine(ModuleDirectory, "1_CoreRegistry/Private"),
			Path.Combine(ModuleDirectory, "2_MemoryManagement/Private"),
			Path.Combine(ModuleDirectory, "3_ThreadingTaskSystem/Private"),
			Path.Combine(ModuleDirectory, "4_EventSystem/Private"),
			Path.Combine(ModuleDirectory, "5_ConfigurationManagement/Private"),
			Path.Combine(ModuleDirectory, "6_ServiceDependencyRegistry/Private"),
			Path.Combine(ModuleDirectory, "7_FactorySystem/Private"),
			
			// Extended systems
			Path.Combine(ModuleDirectory, "25_SvoSdfVolume/Private"),
			Path.Combine(ModuleDirectory, "2.1_TieredCompression/Private"),
			Path.Combine(ModuleDirectory, "2.2_RegionHibernation/Private"),
			Path.Combine(ModuleDirectory, "2.3_ZoneBasedConcurrentMining/Private")
		});

		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput", 
			"Json", 
			"RHI",
			"RenderCore"
		});
		
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});
	}
}
