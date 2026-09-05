// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynavisUE.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FSynavisUEModule"

void FSynavisUEModule::StartupModule()
{
	// Map the plugin's physical Shaders/ folder to the virtual include path
	// /Plugin/SynavisUE/ so Material Custom nodes can #include our Leaf*.ush
	// files. Registered during PostConfigInit so the mapping exists before any
	// material shader compilation.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SynavisUE"));
	if (Plugin.IsValid())
	{
		const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/SynavisUE"), ShaderDirectory);
	}
}

void FSynavisUEModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSynavisUEModule, SynavisUE)