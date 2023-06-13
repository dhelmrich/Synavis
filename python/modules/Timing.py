import numpy as np
import time
import sys
import json
from tqdm import tqdm

# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../build/synavis/Release/")
else :
  sys.path.append("../../build/")
sys.path.append("../modules/")
import PySynavis as rtc

import base64

#rtc.VerboseMode()

message_buffer = []

# a method to reset the message buffer
def reset_message() :
  global message_buffer
  message_buffer = []

# a method to get the next message from the buffer
def get_message(retry_timeout = 5) :
  global message_buffercon
  start_time = time.time()
  while len(message_buffer) == 0 :
    if time.time() - start_time > retry_timeout :
      return None
    time.sleep(0.1)
  message = message_buffer.pop(0)
  return message

# a callback function for the data connector
def message_callback(msg) :
  global message_buffer
  #print("Received message: ", msg[0:20])
  # decode from utf-8
  message_buffer.append(str(msg))

# a callback function for the data connector
def data_callback(data) :
  #print("Received data: ", data)
  pass

def frame_callback(frame) :
  #print("Received frame.")
  pass

m = rtc.MediaReceiver()
#m.IP = "127.0.0.1"
m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
#m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
#m.SetFrameReceptionCallback(frame_callback)

while not m.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

print("Starting")

time.sleep(1)

m.SendJSON({"type":"command", "name":"RawData", "framecapturetime": -2.0})
m.SendJSON({"type":"settings", "bRespondWithTiming":True})

reset_message()

# open a file for the timings
f = open("timings.csv", "w")
# write the columns to the file: test name; message size; send time; processed time; receive time; total time
f.write("test_name;message_size;send_time;processed_time;receive_time;total_time\n")

# construct a cube geometry
def cube_geometry(size, center) :
  vertices = np.array([[-size / 2, -size / 2, -size / 2],
                       [-size / 2, -size / 2, size / 2],
                       [-size / 2, size / 2, -size / 2],
                       [-size / 2, size / 2, size / 2],
                       [size / 2, -size / 2, -size / 2],
                       [size / 2, -size / 2, size / 2],
                       [size / 2, size / 2, -size / 2],
                       [size / 2, size / 2, size / 2]]).astype("float64")
  vertices += center
  indices = np.array([[0, 1, 3], [0, 3, 2],
                      [0, 4, 5], [0, 5, 1],
                      [0, 2, 6], [0, 6, 4],
                      [7, 5, 4], [7, 4, 6],
                      [7, 6, 2], [7, 2, 3],
                      [7, 3, 1], [7, 1, 5]]).astype("int32")
  # turn all indices around
  indices = np.flip(indices, axis = 1)
  normals = np.array([[-1, -1, -1],
                      [-1, -1, 1],
                      [-1, 1, -1],
                      [-1, 1, 1],
                      [1, -1, -1],
                      [1, -1, 1],
                      [1, 1, -1],
                      [1, 1, 1]]).astype("float64")
  return vertices, indices, normals

# a method to send a message and time the process
def send_message(message, test_name, message_size, num_repetitions = 1) :
  global message_buffer, m
  # repeat the process
  for i in tqdm(range(num_repetitions),"Collecting Statistics") :
    # reset the message buffer
    reset_message()
    # wait a bit
    time.sleep(0.1)
    # get the current time
    start_time = time.time()
    response = None
    send_time = None
    receive_time = None
    while response == None :
      send_time = time.time()
      m.SendJSON(message)
      response = get_message()
      receive_time = time.time()
    #print(message)
    response_json = json.loads(response)
    # extract the time from the message
    processed_time = response_json["processed_time"]
    # calculate the total time
    total_time = receive_time - start_time
    # write the data to the file
    f.write(test_name + ";" + str(message_size) + ";" + str(send_time - start_time) + ";" + str(processed_time - send_time) + ";" + str(receive_time - processed_time) + ";" + str(total_time) + "\n")

num_rep = 20
# query message
message = {"type": "query"}
send_message(message, "query", len(str(message)), num_rep)
# query of properties
print("query of properties")
message = {"type": "query", "object": "BP_SynavisDrone_C_1"}
send_message(message, "query of properties", len(str(message)), num_rep)
#query of value
print("query of value")
message = {"type": "query", "object": "BP_SynavisDrone_C_1", "property": "MaxVelocity"}
send_message(message, "query of value", len(str(message)), num_rep)
# setting a value
print("setting a value")
message = {"type": "parameter", "object": "BP_SynavisDrone_C_1", "property": "MaxVelocity", "value": 100}
send_message(message, "setting a value", len(str(message)), num_rep)

# creation of a geometry
# random position
center = np.random.rand(3) * 100
# random size
size = np.random.rand() * 50 + 50
# create the geometry
points, triangles, normals = cube_geometry(size, center)
print("creation of a geometry")
message = {"type": "directbase64", "random":1, "points": base64.b64encode(points.flatten()).decode("utf-8"), "triangles": base64.b64encode(triangles.flatten()).decode("utf-8"), "normals": base64.b64encode(normals.flatten()).decode("utf-8"), "name": "box"}
send_message(message, "creation of a geometry", len(str(message)), num_rep)

f.close()
