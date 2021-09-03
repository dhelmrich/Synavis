#include <iostream>
#include <filesystem>
#include "UnrealReceiver.h"
#include <chrono>
#include <thread>

int main()
{
  std::cout << std::filesystem::current_path() << std::endl;
  UnrealReceiver r;
  r.UseConfig("./config.json");
  r.RegisterWithSignalling();

  
  while (true)
  {
    switch (r.State())
    {
      case EConnectionState::STARTUP:
        break;
      case EConnectionState::SIGNUP:
        r.Offer();
        break;
      case EConnectionState::OFFERED:
        break;
      case EConnectionState::CONNECTED:
        break;
      case EConnectionState::VIDEO:
        break;
      case EConnectionState::CLOSED:
        break;
      case EConnectionState::ERROR:
        std::cout << "Shutting down due to error." << std::endl;
        return EXIT_FAILURE;
      default:
        break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return EXIT_SUCCESS;
}
