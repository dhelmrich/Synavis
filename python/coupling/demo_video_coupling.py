import sys
import numpy as np
import base64
import time
import json

sys.path.append("../../build_unix/")

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

import PySynavis as rtc

rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogDebug)

dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "172.20.16.1", "SignallingPort": 8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)
dataconnector.LockUntilConnected(500)

print("Testing coupling ...")
print("Trying to set the time in UE...")
dataconnector.SendJSON({"type":"t", "s": "2024.06.01-21.00.00"})
time.sleep(1)
dataconnector.SendJSON({"type":"t", "s": "2024.06.01-17.00.00"})
time.sleep(1)
dataconnector.SendJSON({"type":"t", "s": "2024.06.01-13.00.00"})
time.sleep(1)
dataconnector.SendJSON({"type":"spawnmeter", "number": 8})
print("Trying to setup a plant field")
dataconnector.SendJSON({"type":"placeplant", 
                        "number": 100*4,
                        "rule": "square",
                        "mpi_world_size": 4,
                        "mpi_rank": 0,
                        "spacing": 50.0,
                      })
time.sleep(5)
print("Trying to put a plant in the field")
# simple box geometry as a plant
points = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]]).astype("float64") * 5.0
normals = np.array([[0, 0, -1], [0, 0, -1], [0, 0, -1], [0, 0, -1], [0, 0, 1], [0, 0, 1], [0, 0, 1], [0, 0, 1]]).astype("float64")
triangles = np.array([[0, 1, 2], [0, 2, 3], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [2, 3, 7], [2, 7, 6], [0, 3, 7], [0, 7, 4], [1, 2, 6], [1, 6, 5]]).astype("int32")
id = 0
for i in range(20) :
  for j in range(100) :
    random_displacement = np.random.rand(3) * 20.0
    #put z component to 10
    random_displacement[2] = np.min([random_displacement[2], 0.0]) + 10.0
    dataconnector.SendJSON({"type":"do",
                        "p":base64.b64encode(points + random_displacement).decode('utf-8'),
                        "n":base64.b64encode(normals).decode('utf-8'),
                        "i":base64.b64encode(triangles).decode('utf-8'),
                        "o":4,
                        "l":j,
                        })
    #time.sleep(0.1)
print("Trying to measure at the plant")
dataconnector.SendJSON({
  "type":"mm",
  "l":id,
  "d":1.0,
  "p": base64.b64encode(np.array([[0.5, 0.5, 100]])).decode('utf-8')
})

time.sleep(10)

print("Wait until the measurement is done")
message = get_message()

def check_message(message) :
  try :
    message = json.loads(message)
  except :
    return False
  if message["type"] == "mm" :
    return True

while not check_message(message) :
  message = get_message()

print("Received message: ", message)

