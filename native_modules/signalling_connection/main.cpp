#include "Provider.hpp"
#include <json.hpp>

// Substitution might be happening, so this will make it discoverable through
// the refactoring and code assist tools without being too specific here
using json = WebRTCBridge::Bridge::json;

int main()
{
  auto Bridge = std::make_shared<WebRTCBridge::Provider>();
  json Config = {{}};
}
