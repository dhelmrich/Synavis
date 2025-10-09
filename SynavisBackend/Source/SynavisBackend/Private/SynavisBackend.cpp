// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynavisBackend.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FSynavisBackendModule"

void FSynavisBackendModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Get the base directory of this plugin
	FString BaseDir = IPluginManager::Get().FindPlugin("SynavisBackend")->GetBaseDir();

	// Add on the relative location of the libdatachannel dll and load it
	FString LibraryPath;
#if PLATFORM_WINDOWS
	// On Windows we ship the prebuilt datachannel DLL in the plugin's Source/libdatachannel/lib folder
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/libdatachannel/lib/datachannel.dll"));
#elif PLATFORM_MAC
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/libdatachannel/lib/libdatachannel.dylib"));
#elif PLATFORM_LINUX
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/libdatachannel/lib/libdatachannel.so"));
#endif // PLATFORM_WINDOWS

	LibDataChannelHandle = !LibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*LibraryPath) : nullptr;

	if (LibDataChannelHandle)
	{
		// libdatachannel does not require a single explicit entry-point to start.
		// Optionally we can call into its C API if exposed. For now just log success.
		UE_LOG(LogTemp, Log, TEXT("Loaded libdatachannel from %s"), *LibraryPath);
        
		// Example: if you exposed an init symbol in a custom wrapper, you could call it like this:
		// typedef void (*InitFunc)();
		// InitFunc Init = (InitFunc)FPlatformProcess::GetDllExport(LibDataChannelHandle, TEXT("YourInitFunction"));
		// if (Init) { Init(); }
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ThirdPartyLibraryError", "Failed to load libdatachannel (datachannel.dll)"));
	}
}

void FSynavisBackendModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Free the dll handle
	FPlatformProcess::FreeDllHandle(LibDataChannelHandle);
	LibDataChannelHandle = nullptr;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSynavisBackendModule, SynavisBackend)
