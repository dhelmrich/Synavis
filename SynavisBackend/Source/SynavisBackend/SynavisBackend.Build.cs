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
				// Rendering and shader dependencies for GPU conversion
				"RenderCore",
				"RHI",
				"Renderer",
				
				// ... add other private dependencies that you statically link with here ...
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

		// ---- libvpx support (flat layout under Source/libvpx)
		string LibVpxInclude = System.IO.Path.Combine(ModuleDirectory, "..", "libvpx", "include");
		LibVpxInclude = System.IO.Path.GetFullPath(LibVpxInclude);
		PublicSystemIncludePaths.Add(LibVpxInclude);
		if (!System.IO.Directory.Exists(LibVpxInclude))
		{
			System.Console.WriteLine($"Warning: libvpx include directory not found: {LibVpxInclude}. Run the CMake copy step to populate Source/libvpx/include.");
		}

		string LibVpxPath = System.IO.Path.Combine(ModuleDirectory, "..", "libvpx", "lib");
		LibVpxPath = System.IO.Path.GetFullPath(LibVpxPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Prefer DLL + import lib if present
			var dlls = System.IO.Directory.Exists(LibVpxPath) ? System.IO.Directory.GetFiles(LibVpxPath, "vpx*.dll") : new string[0];
			var libs = System.IO.Directory.Exists(LibVpxPath) ? System.IO.Directory.GetFiles(LibVpxPath, "vpx*.lib") : new string[0];

			if (dlls.Length > 0 && libs.Length > 0)
			{
				// Use first matched dll/lib pair
				string dllPath = dlls[0];
				string dllName = System.IO.Path.GetFileName(dllPath);
				string importLib = libs[0];
				PublicAdditionalLibraries.Add(importLib);
				PublicDelayLoadDLLs.Add(dllName);
				RuntimeDependencies.Add(dllPath);
				PublicDefinitions.Add("LIBVPX_AVAILABLE=1");
			}
			else if (libs.Length > 0)
			{
				// Static or import libs only
				foreach (var lib in libs)
				{
					PublicAdditionalLibraries.Add(lib);
				}
				PublicDefinitions.Add("LIBVPX_AVAILABLE=1");
			}
			else if (dlls.Length > 0)
			{
				// DLLs exist but no import lib found. Warn and still add runtime dependency so UE can stage the DLL.
				foreach (var dll in dlls)
				{
					string dllName = System.IO.Path.GetFileName(dll);
					System.Console.WriteLine($"Warning: Found {dllName} in {LibVpxPath} but no corresponding import lib (.lib) was found. Linking may fail.");
					RuntimeDependencies.Add(dll);
					PublicDefinitions.Add("LIBVPX_AVAILABLE=1");
				}
			}
			else
			{
				System.Console.WriteLine($"Notice: No libvpx artifacts found in {LibVpxPath}.");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string so = System.IO.Path.Combine(LibVpxPath, "libvpx.so");
			if (System.IO.File.Exists(so))
			{
				PublicAdditionalLibraries.Add(so);
				RuntimeDependencies.Add(so);
				PublicDefinitions.Add("LIBVPX_AVAILABLE=1");
			}
			else
			{
				// Add .a static libs if present
				var staticLibs = System.IO.Directory.Exists(LibVpxPath) ? System.IO.Directory.GetFiles(LibVpxPath, "libvpx*.a") : new string[0];
				foreach (var a in staticLibs) PublicAdditionalLibraries.Add(a);
			}
		}
	}
}
