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


  return r.RunForever();
}
