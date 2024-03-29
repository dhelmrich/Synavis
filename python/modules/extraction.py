import numpy as np
#import tensorflow as tf
#import tensorflow.keras.backend as K
#import horovod.tensorflow.keras as hvd
import cv2
import threading
import time
import sys
import os
import json

# Synavis: Find build
path = "../"
# if windows
if os.name == 'nt' :
  path = path + "build/synavis/Release/"
  print(path)
else :
  path = path + "build/"
sys.path.append(path)
import PySynavis as rtc
from signalling_server import start_signalling

#start_signalling(False)

HEIGHT = 512
WIDTH = 512

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

def frame_callback(frame) :
  print("Received frame.")

m = rtc.MediaReceiver()
f = rtc.FrameDecode()
f.SetFrameCallback(frame_callback)
m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
m.SetFrameReceptionCallback(f.CreateAcceptor(data_callback))

while not m.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

print("Starting")
