#include <iostream>
#include <vector>
#include <rtc/rtc.h>

int main()
{
  rtcSctpSettings sctpsettings;
  sctpsettings.maxBurst = 0;
  std::cout << "Test finished" << std::endl;
}
