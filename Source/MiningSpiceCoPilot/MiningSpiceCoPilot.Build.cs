// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MiningSpiceCoPilot : ModuleRules
{
	public MiningSpiceCoPilot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		// Force use of specific PCH for the service registry
		PrivatePCHHeaderFile = "6_ServiceRegistryandDependency/Private/ServiceRegistryPCH.h";
		
		// Add public include paths for all our systems
		PublicIncludePaths.AddRange(new string[] {
			// Core systems
			Path.Combine(ModuleDirectory, "1_CoreRegistry/Public"),
			Path.Combine(ModuleDirectory, "2_MemoryManagement/Public"),
			Path.Combine(ModuleDirectory, "3_ThreadingTaskSystem/Public"),
			Path.Combine(ModuleDirectory, "6_ServiceRegistryandDependency/Public"),
			
			// Extended systems
			Path.Combine(ModuleDirectory, "2.1_TieredCompression/Public"),
			Path.Combine(ModuleDirectory, "2.2_RegionHibernation/Public"),
			Path.Combine(ModuleDirectory, "2.3_ZoneBasedConcurrentMining/Public"),
            
            // Logging
            Path.Combine(ModuleDirectory, "Public/Logging"),
		});
		
		// Add private include paths
		PrivateIncludePaths.AddRange(new string[] {
			// Core systems
			Path.Combine(ModuleDirectory, "1_CoreRegistry/Private"),
			Path.Combine(ModuleDirectory, "2_MemoryManagement/Private"),
			Path.Combine(ModuleDirectory, "3_ThreadingTaskSystem/Private"),
			Path.Combine(ModuleDirectory, "6_ServiceRegistryandDependency/Private"),
			
			// Extended systems
			Path.Combine(ModuleDirectory, "2.1_TieredCompression/Private"),
			Path.Combine(ModuleDirectory, "2.2_RegionHibernation/Private"),
			Path.Combine(ModuleDirectory, "2.3_ZoneBasedConcurrentMining/Private"),
            
            // Logging
            Path.Combine(ModuleDirectory, "Private/Logging"),
		});

		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput", 
			"Json", 
			"JsonUtilities", // Add JsonUtilities for working with JSON
			"RHI",
			"RenderCore",
			"ApplicationCore" // Additional platform abstraction
		});
		
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"Projects",   // For project settings and access
			"EngineSettings" // For engine configuration access
		});
		
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
        
        // Ensure strict include order
        bEnforceIWYU = true;
        
        // Use Unity builds to improve compilation times
        bUseUnity = true;
	}
}
