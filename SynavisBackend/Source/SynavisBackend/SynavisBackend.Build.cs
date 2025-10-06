using UnrealBuildTool;
using System.IO;

public class SynavisBackend : ModuleRules
{
    public SynavisBackend(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        string SynavisBackendPath = Path.Combine(ModuleDirectory, "../../lib");

        PublicIncludePaths.Add(Path.Combine(SynavisBackendPath, "include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(SynavisBackendPath, "win64", "datachannel.lib"));
            RuntimeDependencies.Add("$(PluginDir)/lib/win64/datachannel.dll");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(SynavisBackendPath, "linux", "SynavisBackend.a"));
            RuntimeDependencies.Add("$(PluginDir)/lib/linux/SynavisBackend.so");
        }
    }
}
