import numpy as np
import scipy
import scipy.optimize
import scipy.stats
import matplotlib.pyplot as plt
import time
import sys
import os

message_buffer = []

# a method to reset the message buffer
def reset_message() :
  global message_buffer
  message_buffer = []

# a method to get the next message from the buffer
def get_message() :
  global message_buffer
  while len(message_buffer) == 0 :
    time.sleep(0.1)
  message = message_buffer.pop(0)
  return message

# a callback function for the data connector
def message_callback(msg) :
  global message_buffer
  print("Received message: ", msg)
  # decode from utf-8
  message_buffer.append(str(msg))

# a callback function for the data connector
def data_callback(data) :
  print("Received data: ", data)

# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../build/synavis/Release/")
else :
  sys.path.append("../unix/")
sys.path.append("../modules/")
import PySynavis as rtc

rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogDebug)

#make the data connector
dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "172.20.16.1","SignallingPort":8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetDataCallback(data_callback)
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)

while not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

reset_message()

intensity_levels = np.arange(0, 10000, 1000)
emissive_boosts = np.arange(0, 10000, 1000)
# technically, we now need to sample 1000*1000 = 1e6 points
intensities = []

dataconnector.SendJSON({"type":"command", "name":"cam", "camera": "scene"})
dataconnector.SendJSON({"type":"settings", "bVagueMatchProperties":True})

schedule = {"type": "schedule", "command": {
  "type": "query",
  "property": "Intensity",
  "object": "LightMeter"
  },
  "time": 0.0,
  "repeat": 1.0
}

dataconnector.SendJSON(schedule)

for intensity in intensity_levels :
  for emissive_boost in emissive_boosts :
    dataconnector.SendJSON({"type":"command", "name":"set", "property": "EmissiveBoost", "value": emissive_boost})
    dataconnector.SendJSON({"type":"command", "name":"set", "property": "Intensity", "value": intensity})
    t0 = time.time()
    while time.time() - t0 < 2.0 :
      message = get_message()
      if message is not None :
        intensities.append(int(message))
        break
  

while True :
  time.sleep(1)
