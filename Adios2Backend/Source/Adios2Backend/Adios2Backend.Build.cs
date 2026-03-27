using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;

public class Adios2Backend : ModuleRules
{
    public Adios2Backend(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
            new string[] {
                // ... add public include paths required here ...
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                "Adios2Backend/Private",
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "SynavisUE"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Add other private dependencies that you statically link with here
            }
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                // ...
            }
        );

        // Add the ADIOS2 include directory
        string Adios2Include = Path.Combine(ModuleDirectory, "..", "adios2", "include");
        Adios2Include = Path.GetFullPath(Adios2Include);
        PublicSystemIncludePaths.Add(Adios2Include);

        if (!Directory.Exists(Adios2Include))
        {
            System.Console.WriteLine($"Warning: ADIOS2 include directory not found: {Adios2Include}. Run the CMake copy step to populate Source/adios2/include.");
        }

        string Adios2LibPath = Path.Combine(ModuleDirectory, "..", "adios2", "lib");
        Adios2LibPath = Path.GetFullPath(Adios2LibPath);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            if (!Directory.Exists(Adios2LibPath))
            {
                throw new System.Exception($"Required ADIOS2 library directory not found: {Adios2LibPath}. Please place ADIOS2 import libs (.lib) and DLLs (.dll) in Source/adios2/lib.");
            }

            string[] wanted = new string[] { "adios2" };
            var libs = Directory.GetFiles(Adios2LibPath, "*.lib");
            var dlls = Directory.GetFiles(Adios2LibPath, "*.dll");

            if (libs.Length == 0 && dlls.Length == 0)
            {
                throw new System.Exception($"Required ADIOS2 artifacts not found in {Adios2LibPath}. Expected '*.lib' and '*.dll'.");
            }

            foreach (var lib in libs)
            {
                if (Path.GetFileName(lib).Contains("adios2"))
                {
                    PublicAdditionalLibraries.Add(lib);
                }
            }

            foreach (var dllPath in dlls)
            {
                if (Path.GetFileName(dllPath).Contains("adios2"))
                {
                    string dllName = Path.GetFileName(dllPath);
                    PublicDelayLoadDLLs.Add(dllName);
                    RuntimeDependencies.Add(dllPath);
                }
            }

            PublicDefinitions.Add("ADIOS2_AVAILABLE=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            if (Directory.Exists(Adios2LibPath))
            {
                var soFiles = Directory.GetFiles(Adios2LibPath, "libadios2.so*");
                foreach (var so in soFiles) { PublicAdditionalLibraries.Add(so); RuntimeDependencies.Add(so); }
                PublicDefinitions.Add("ADIOS2_AVAILABLE=1");
            }
            else
            {
                string[] wanted = new string[] { "adios2" };
                foreach (var name in wanted)
                {
                    string soName = "lib" + name + ".so";
                    PublicAdditionalLibraries.Add(soName);
                }
                PublicDefinitions.Add("ADIOS2_AVAILABLE=1");
            }
        }
    }
}
