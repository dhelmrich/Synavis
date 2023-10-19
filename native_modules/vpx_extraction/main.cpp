
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "MediaReceiver.hpp"
#include "FrameDecode.hpp"

using json = nlohmann::json;
inline constexpr std::byte operator "" _b(unsigned long long i) noexcept
{
  return static_cast<std::byte>(i);
}

inline std::byte operator+(std::byte b, std::byte i) noexcept
{
  return static_cast<std::byte>(static_cast<int>(b) + static_cast<int>(i));
}

inline std::byte operator++(std::byte& b) noexcept
{
  return b = b + 1_b;
}


int main()
{
  auto dc = std::make_shared<Synavis::MediaReceiver>();
  dc->Initialize();
  dc->ConfigureRelay("127.0.0.1", 53326);
  dc->SetTakeFirstStep(false);
  dc->SetLogVerbosity(Synavis::ELogVerbosity::Warning);
  auto vpx = std::make_shared<Synavis::FrameDecode>();
  dc->SetDataCallback(vpx->CreateAcceptor([](rtc::binary frame_or_data)
    {
      std::cout << "Got frame or data" << std::endl;
    
    }));
  vpx->SetFrameCallback([](Synavis::Frame frame)
    {
      std::cout << "Got frame (" << frame.Width << "/" << frame.Height << ")" << std::endl;
    });
}
