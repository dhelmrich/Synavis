import sys
import time

sys.path.append("../../build/webrtcbridge/Debug/")

import PyWebRTCBridge as rtc

socket = rtc.BridgeSocket()
socket.Address = "localhost"
socket.Port = 51250
print(socket.Address, ":", socket.Port)

if not socket.Connect() :
  print("could not connect")
else :
  print("could connect")


