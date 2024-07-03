// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SynavisUE : ModuleRules
{
	public SynavisUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
        "Core", "CoreUObject", "Engine",
        "DynamicMesh", 
        "UMG", "Foliage","Json", 
        "Landscape", "Niagara",
        "ModelingComponents",
        "ProceduralMeshComponent", 
        "PixelStreaming",
        "PixelStreamingBlueprint",
        // ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore","DynamicMesh", "PixelStreaming"
                // ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
