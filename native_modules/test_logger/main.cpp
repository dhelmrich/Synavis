#include <iostream>
#include <string>
#include <vector>
#include <span>

#include "Synavis.hpp"

using namespace Synavis;

int main()
{
  std::vector<double> ddata = {1.0, 2.0, 3.0, 4.0};
  auto encoded = Encode64(ddata);
  if(encoded != "AAAAAAAA8D8AAAAAAAAAQAAAAAAAAAhAAAAAAAAAEEA=")
  {
    std::cout << "Encode64 failed" << std::endl;
    std::cout << "Expected: AAAAAAAA8D8AAAAAAAAAQAAAAAAAAAhAAAAAAAAAEEA=" << std::endl;
    std::cout << "Got: " << encoded << std::endl;
    return 1;
  }
  delete [] encoded.data();
  std::vector<float> fdata = {1.0, 2.0, 3.0, 4.0};
  encoded = Encode64(fdata);
  if(encoded != "AACAPwAAAEAAAEBAAACAQA==")
  {
    std::cout << "Encode64 failed" << std::endl;
    std::cout << "Expected: AACAPwAAAEAAAEBAAACAQA==" << std::endl;
    std::cout << "Got: " << encoded << std::endl;
    return 1;
  }
  std::vector<int32_t> idata = {1, 2, 3, 4};
  encoded = Encode64(idata);
  if(encoded != "AQAAAAIAAAADAAAABAAAAA==")
  {
       std::cout << "Encode64 failed" << std::endl;
    std::cout << "Expected: AQAAAAIAAAADAAAABAAAAA==" << std::endl;
    std::cout << "Got: " << encoded << std::endl;
    return 1;
  }
  std::vector<int64_t> ldata = {1, 2, 3, 4};
  encoded = Encode64(ldata);
  if(encoded != "AQAAAAAAAAACAAAAAAAAAAMAAAAAAAAABAAAAAAAAAA=")
  {
    std::cout << "Encode64 failed" << std::endl;
    std::cout << "Expected: AQAAAAAAAAACAAAAAAAAAAMAAAAAAAAABAAAAAAAAAA=" << std::endl;
    std::cout << "Got: " << encoded << std::endl;
    return 1;
  }
  std::vector<int16_t> sdata = {1, 2, 3, 4};
  encoded = Encode64(sdata);
  if(encoded != "AQACAAMABAA=")
  {
       std::cout << "Encode64 failed" << std::endl;
    std::cout << "Expected: AQACAAMABAA=" << std::endl;
    std::cout << "Got: " << encoded << std::endl;
    return 1;
  }
}
