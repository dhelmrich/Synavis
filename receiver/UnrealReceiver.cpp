#include "UnrealReceiver.h"
#include<fstream>

UnrealReceiver::~UnrealReceiver()
{

}

void UnrealReceiver::RegisterWithSignalling()
{
 std::string address =  "ws://" + config_["PublicIp"].get<std::string>()
  + ":" + config_["HttpPort"].get<std::string>();
  ss_.open(address);
  if(ss_.isOpen())
  {
    
  }
}

void UnrealReceiver::UseConfig(std::string filename)
{
  std::ifstream f(filename);
  if(!f.good())
    return;
  config_ = json::parse(f);
}
