// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SynavisBackend : ModuleRules
{
	public SynavisBackend(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		// Add the flat libdatachannel include directory inside the plugin Source (normalized absolute path)
		string LibDataChannelInclude = System.IO.Path.Combine(ModuleDirectory, "..", "libdatachannel", "include");
		LibDataChannelInclude = System.IO.Path.GetFullPath(LibDataChannelInclude);
		// Use system include to avoid warnings about C++ headers in third-party layout
		PublicSystemIncludePaths.Add(LibDataChannelInclude);

		// If it doesn't exist yet (CMake hasn't been run/copied), warn with a helpful message
		if (!System.IO.Directory.Exists(LibDataChannelInclude))
		{
			System.Console.WriteLine($"Warning: libdatachannel include directory not found: {LibDataChannelInclude}. Run the CMake copy step to populate Source/libdatachannel/include.");
		}
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// The module exposes an ActorComponent in its public headers, so expose Engine and CoreUObject
				"CoreUObject",
				"Engine",
				// libdatachannel is consumed via headers/libs inside Source/libdatachannel
				"Projects"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

	// Platform-specific linking for libdatachannel when using the flat layout under Source/libdatachannel
	// ModuleDirectory points to .../Source/SynavisBackend; the CMake step copies libs to Source/libdatachannel/lib
	string LibDataChannelPath = System.IO.Path.Combine(ModuleDirectory, "..", "libdatachannel", "lib");
	LibDataChannelPath = System.IO.Path.GetFullPath(LibDataChannelPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string DllPath = System.IO.Path.Combine(LibDataChannelPath, "datachannel.dll");
			string LibPath = System.IO.Path.Combine(LibDataChannelPath, "datachannel.lib");
			PublicAdditionalLibraries.Add(LibPath);
			PublicDelayLoadDLLs.Add("datachannel.dll");
			RuntimeDependencies.Add(DllPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string SoPath = System.IO.Path.Combine(LibDataChannelPath, "libdatachannel.so");
			PublicAdditionalLibraries.Add(SoPath);
			RuntimeDependencies.Add(SoPath);
		}
	}
}
