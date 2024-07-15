import numpy as np
import scipy
import scipy.optimize
import scipy.stats
import matplotlib.pyplot as plt
import time
import sys
import os
import json

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
  #print("Received message: ", msg)
  # decode from utf-8
  message_buffer.append(str(msg))

# a callback function for the data connector
def data_callback(data) :
  print("Received data: ", data)

# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../build_win/synavis/Release/")
else :
  sys.path.append("../unix/")
sys.path.append("../modules/")
import PySynavis as rtc

rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogError)

#make the data connector
dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetDataCallback(data_callback)
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)

while not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

reset_message()

def logscale(start, end, number) :
  return np.logspace(np.log10(start), np.log10(end), number)

intensity_levels = logscale(13.50287, 18334254.0, 500)
emissive_levels = logscale(1, 10000, 10)
# technically, we now need to sample 1000*1000 = 1e6 points
intensities = []
emissive_boosts = []
measurements = []

dataconnector.SendJSON({"type":"query"})

#dataconnector.SendJSON({"type":"command", "name":"cam", "camera": "scene"})

# for rapid testing, we can use a vague match
# TODO: remove this line for cluster execution
dataconnector.SendJSON({"type":"settings", "bVagueMatchProperties":True})
dataconnector.SendJSON({"type":"console", "command":"t.maxFPS 10"})
dataconnector.SendJSON({"type":"console", "command":"r.PathTracing.ProgressDisplay 0"})

reset_message()

schedule = {"type": "schedule", "command": {
  "type": "query",
  "all": [
    {"object":"BPLightMeter_C_1.Intensity","property":"Intensity"},
    {"object":"SunSky_C_1.DirectionalLight","property":"Intensity"},
  ]
  },
  "time": 0.0,
  "repeat": 0.2
}

dataconnector.SendJSON(schedule)

current_intensity = 0.0
current_emissive_boost = 0.0

def storage_callback(message) :
  # message like {"type":"query","name":"BPLightMeter_C_1.Intensity","data":{"value":0.000000}}
  global intensities, emissive_boosts, measurements, current_intensity, current_emissive_boost
  #print(message)
  try :
    message = json.loads(message)
    measurements.append(float(message["data"]["BPLightMeter_C_1.Intensity"]["data"]["value"]))
    intensities.append(float(message["data"]["SunSky_C_1.DirectionalLight"]["data"]["value"]))
    emissive_boosts.append(current_emissive_boost)
  except Exception as e:
    pass
  #endtry
#enddef

dataconnector.SetMessageCallback(storage_callback)


for emissive_boost in emissive_levels :
  for intensity in intensity_levels :
    print("Combination of: ", intensity, " Lumen and ", emissive_boost, " Emission")
    current_intensity = intensity
    current_emissive_boost = emissive_boost
    dataconnector.SendJSON({
      "type":"material",
      "slot":"CallibrationMaterial",
      "object":"StaticMeshActor",
      "dtype":"scalar",
      "parameter":"Emissive",
      "value": float(emissive_boost)
    })
    dataconnector.SendJSON({
      "type": "parameter",
      "object": "SunSky_C_1.DirectionalLight",
      "property": "Intensity",
      "value": float(intensity)
    })
    time.sleep(0.5)
  #endfor
#endfor

# export to csv
import pandas as pd
df = pd.DataFrame({"intensity": intensities, "emissive_boost": emissive_boosts, "measurement": measurements})

df.to_csv("measurements" + str(time.time()) + ".csv")
