import sys
import time

sys.path.append("../../build/synavis/Debug/")

import PySynavis as rtc

socket = rtc.BridgeSocket()
socket.Address = "localhost"
socket.Port = 51250
print(socket.Address, ":", socket.Port)

if not socket.Connect() :
  print("could not connect")
else :
  print("could connect")


