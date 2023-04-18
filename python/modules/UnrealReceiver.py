import numpy as np
#import tensorflow as tf
#import tensorflow.keras.backend as K
#import horovod.tensorflow.keras as hvd
import cv2
import threading
import time
import sys
import os

# WebRTCBridge: Find build
path = "../../"
# if windows
if os.name == 'nt' :
  path = path + "build_workwin/webrtcbridge/Release/"
  print(path)
else :
  path = path + "build/"
sys.path.append(path)
import PyWebRTCBridge as rtc
from signalling_server import start_signalling

#start_signalling(True)

HEIGHT = 512
WIDTH = 512

# a callback function for the data connector
def message_callback(msg) :
  print("message: ", msg)
  global message_buffer
  global message_ready
  message_buffer = msg
  message_ready = True

# a callback function for the data connector
def data_callback(data) :
  print("Received data: ", data)

def frame_callback(frame) :
  print("Received frame.")

Media = rtc.MediaReceiver()
#Media.SetConfigFile("config.json")
Media.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
Media.SetTakeFirstStep(False)
Media.StartSignalling()
Media.SetDataCallback(data_callback)
Media.SetMessageCallback(message_callback)
Media.SetFrameReceptionCallback(frame_callback)

while not Media.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

print("Starting")


while True:
  time.sleep(0.1)
  Media.SendJSON({"type":"info", "frametime":0})

