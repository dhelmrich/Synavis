using UnrealBuildTool;
using System.IO;

public class SynavisBackend : ModuleRules
{
    public SynavisBackend(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        string SynavisBackendRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
        string LibPath = Path.Combine(SynavisBackendRoot, "lib");
        string IncludePath = Path.Combine(SynavisBackendRoot, "include");

        // Add all relevant include paths
        PublicIncludePaths.Add(IncludePath);
        PublicIncludePaths.Add(Path.Combine(IncludePath, "json"));
        PublicIncludePaths.Add(Path.Combine(IncludePath, "rtc"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "datachannel.lib"));
            RuntimeDependencies.Add("$(PluginDir)/lib/datachannel.dll");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "datachannel.a"));
            RuntimeDependencies.Add("$(PluginDir)/lib/datachannel.so");
        }
    }
}
