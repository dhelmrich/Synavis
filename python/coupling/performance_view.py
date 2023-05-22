from progressbar import progressbar as printsafebar
from progress.spinner import MoonSpinner as spinner

import sys
import os
import numpy as np
import base64
import subprocess

# check if CPLANTBOX_ROOT is set
if "CPLANTBOX_ROOT" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_ROOT"])
else :
  print("CPLANTBOX_ROOT is not set, but I will try to import plantbox from your PYTHONPATH")
try :
  import plantbox as pb
except Exception as e :
  print("Error: Could not import plantbox, please set CPLANTBOX_ROOT or add plantbox to your PYTHONPATH")
  sys.exit(1)

# a buffer for incoming messages and a flag to indicate if a message is readys
message_buffer = ""
message_ready = False

# a callback function for the data connector
def message_callback(msg) :
  global message_buffer
  global message_ready
  message_buffer = msg
  message_ready = True


# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../../build/synavis/Debug/")
else :
  sys.path.append("../../build/")
sys.path.append("../modules/")
import PySynavis as rtc
import signalling_server as ss
# 

# Start Unreal from the command line
# it should be located in the demo folder of the base project
unreal_path = "../../demo/MinimalWebRTC/MinimalWebRTC"
if sys.platform == "win32" :
  unreal_path += ".exe"
elif sys.platform == "linux" :
  unreal_path += ".so"

print("Starting Unreal Engine at " + unreal_path)
# start unreal engine in process so that we can kill it later
unreal = subprocess.Popen(unreal_path, shell=True, stdout="unreal.log", stderr="unreal.log")

ss.start_signalling_server()

#make the data connector
dc = rtc.DataConnector()
dc.SetConfig("config.json")
dc.StartSignalling()
dc.SetMessageCallback(message_callback)



