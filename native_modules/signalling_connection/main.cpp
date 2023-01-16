#include "Provider.hpp"
#include <json.hpp>

// Substitution might be happening, so this will make it discoverable through
// the refactoring and code assist tools without being too specific here
using json = WebRTCBridge::Bridge::json;

int main()
{
  auto Bridge = std::make_shared<WebRTCBridge::Provider>();
  json Config{
      {
        {"LocalPort", 3030},
        {"RemotePort",3031},
        {"LocalAddress","localhost"},
        {"RemoteAddress","localhost"},
        {"Signalling",int()}
      }};
  Bridge->UseConfig(Config);
  Bridge->StartSignalling("127.0.0.1", 8889);
  std::this_thread::sleep_for(std::chrono::seconds(5));
  json test = {{"type", "hi"}};
  std::cout << "Sending: " << test.dump() << std::endl;
  Bridge->SubmitToSignalling(test, nullptr);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  Bridge->Stop();
}
