import sys
import time

sys.path.append("../../build/webrtcbridge/Debug/")

import PyWebRTCBridge as rtc

Bridge = rtc.Provider()
Client = rtc.DataConnector()

print("Done in Python")

time.sleep(4)

print("Testing Connection")
if Bridge.EstablishedConnection(True) :
  print("Shallow testing of Bridge Connections succeeded!")
else :
  print("Shallow testing of bridge connections failed!")
