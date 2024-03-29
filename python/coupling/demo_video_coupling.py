import sys
import numpy as np
import base64

sys.path.append("../../build")

import plantbox as pb

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

import PySynavis as rtc

dataconnector = rtc.MediaReceiver()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "127.0.0.1", "SignallingPort": 8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)
