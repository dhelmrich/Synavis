using UnrealBuildTool;
using System.Collections.Generic;

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
    }
}
