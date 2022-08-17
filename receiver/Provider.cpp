#include "Provider.h"

void UR::Provider::ConnectToSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
}

void UR::Provider::CreateTask(std::function<void()>&& Task)
{
}

void UR::Provider::BridgeSynchronize(UR::UnrealReceiver* Instigator, json Message, bool bFailIfNotResolved)
{
}

void UR::Provider::BridgeSubmit(UR::UnrealReceiver* Instigator, std::variant<rtc::binary, std::string> Message) const
{
}

void UR::Provider::BridgeRun()
{
}

void UR::Provider::Listen()
{
}
