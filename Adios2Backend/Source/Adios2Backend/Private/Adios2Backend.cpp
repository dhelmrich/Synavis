#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class FAdios2BackendModule : public IModuleInterface
{
public:
  virtual void StartupModule() override
  {
    UE_LOG(LogTemp, Log, TEXT("Adios2Backend: StartupModule"));
  }

  virtual void ShutdownModule() override
  {
    UE_LOG(LogTemp, Log, TEXT("Adios2Backend: ShutdownModule"));
  }
};

IMPLEMENT_MODULE(FAdios2BackendModule, Adios2Backend)
