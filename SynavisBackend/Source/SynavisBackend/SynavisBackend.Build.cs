// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Diagnostics;

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
				"CoreUObject",
				"Engine",
				"Projects",
				"Json",
				"JsonUtilities",
        "SynavisUE"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				"Renderer",
				"Engine",
				"VulkanRHI"
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
    string ModuleSourceDirectory = System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory));
    string LibDataChannelPath = System.IO.Path.Combine(ModuleDirectory, "..", "libdatachannel", "lib");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string DllPath = System.IO.Path.Combine(LibDataChannelPath, "datachannel.dll");
			string LibPath = System.IO.Path.Combine(LibDataChannelPath, "datachannel.lib");
      PublicAdditionalLibraries.Add(LibPath);
      RuntimeDependencies.Add("$(BinaryOutputDir)/datachannel.dll", DllPath);
      // Also copy OpenSSL dependencies
      string LibSslPath = System.IO.Path.Combine(LibDataChannelPath, "libssl-3-x64.dll");
      string LibCryptoPath = System.IO.Path.Combine(LibDataChannelPath, "libcrypto-3-x64.dll");
      RuntimeDependencies.Add("$(BinaryOutputDir)/libssl-3-x64.dll", LibSslPath);
      RuntimeDependencies.Add("$(BinaryOutputDir)/libcrypto-3-x64.dll", LibCryptoPath);

		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// Prefer a versioned shared object if present (e.g. libdatachannel.so.0.23.2)
			// This avoids packaging an unresolvable symlink `libdatachannel.so` which
			// can confuse the Unreal AutomationTool when copying files between paths.
			try
			{
				var versioned = System.IO.Directory.GetFiles(LibDataChannelPath, "libdatachannel.so.*");
				if (versioned != null && versioned.Length > 0)
				{
					// Register all matching versioned SOs and use them for linking/runtime
					foreach (var so in versioned)
					{
						PublicAdditionalLibraries.Add(so);
						RuntimeDependencies.Add(so);
					}
				}
				else
				{
					// Fallback to the unversioned soname if no versioned file is present
					string SoPath = System.IO.Path.Combine(LibDataChannelPath, "libdatachannel.so");
					PublicAdditionalLibraries.Add(SoPath);
					RuntimeDependencies.Add(SoPath);
				}
			}
			catch (System.Exception)
			{
				// If the directory cannot be read or no files exist, still add the canonical
				// soname path so build will report a clear error later rather than crashing here.
				string SoPath = System.IO.Path.Combine(LibDataChannelPath, "libdatachannel.so");
				PublicAdditionalLibraries.Add(SoPath);
				RuntimeDependencies.Add(SoPath);
			}
		}


		// ---- libav/ffmpeg support (flat layout under Source/libav)
		string LibAvInclude = System.IO.Path.Combine(ModuleDirectory, "..", "libav", "include");
		LibAvInclude = System.IO.Path.GetFullPath(LibAvInclude);
		PublicSystemIncludePaths.Add(LibAvInclude);
		if (!System.IO.Directory.Exists(LibAvInclude))
		{
			System.Console.WriteLine($"Warning: libav include directory not found: {LibAvInclude}. Run the CMake copy step to populate Source/libav/include.");
		}

		string LibAvPath = System.IO.Path.Combine(ModuleDirectory, "..", "libav", "lib");
		LibAvPath = System.IO.Path.GetFullPath(LibAvPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Require common ffmpeg import libs and/or DLLs to be present under Source/libav/lib
			string[] wanted = new string[] { "avcodec", "avformat", "avutil", "swscale", "swresample" };
			if (!System.IO.Directory.Exists(LibAvPath))
			{
				throw new System.Exception($"Required libav library directory not found: {LibAvPath}. Please place FFmpeg import libs (.lib) and DLLs (.dll) in Source/libav/lib.");
			}

			foreach (var name in wanted)
			{
				var libs = System.IO.Directory.GetFiles(LibAvPath, name + "*.lib");
				var dlls = System.IO.Directory.GetFiles(LibAvPath, name + "*.dll");

				if (libs.Length == 0 && dlls.Length == 0)
				{
					throw new System.Exception($"Required libav artifact for '{name}' not found in {LibAvPath}. Expected '{name}*.lib' or '{name}*.dll'.");
				}

				// Prefer import libs for linking
				if (libs.Length > 0)
				{
					foreach (var lib in libs) PublicAdditionalLibraries.Add(lib);
				}

				// Register DLLs for delay-load and runtime
				if (dlls.Length > 0)
				{
					foreach (var dllPath in dlls)
					{
						string dllName = System.IO.Path.GetFileName(dllPath);
						PublicDelayLoadDLLs.Add(dllName);
						RuntimeDependencies.Add(dllPath);
					}
				}
			}

			// Always define LIBAV_AVAILABLE when we require and link libav
			PublicDefinitions.Add("LIBAV_AVAILABLE=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string SoDir = LibAvPath;
			if (System.IO.Directory.Exists(SoDir))
			{
				// Add core shared objects if present in the plugin-local lib path
				var soFiles = System.IO.Directory.GetFiles(SoDir, "libav*.so*");
				foreach (var so in soFiles) { PublicAdditionalLibraries.Add(so); RuntimeDependencies.Add(so); }
				PublicDefinitions.Add("LIBAV_AVAILABLE=1");
			}
			else
			{
				// ffmpeg should be loaded as module on HPC clusters
        // if ffmpeg is not present, please talk to your administrators before using
        // a vcpkg version or sth.
				string[] wanted = new string[] { "avcodec", "avformat", "avutil", "swscale", "swresample" };
        // we cannot catch a missing library error here: this is due to the fact
        // that the module system will put it directly in path.
        foreach (var name in wanted)
        {
          string soName = "lib" + name + ".so";
          PublicAdditionalLibraries.Add(soName);
        }
        PublicDefinitions.Add("LIBAV_AVAILABLE=1");
			}
		}
	}
}
