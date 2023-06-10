# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../build/synavis/Release/")
else :
  sys.path.append("../../build/")
sys.path.append("../modules/")
import PySynavis as rtc

import numpy as np
import base64
import time


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
  #print("Received data: ", data)
  pass

def frame_callback(frame) :
  #print("Received frame.")
  pass

m = rtc.MediaReceiver()
m.IP = "127.0.0.1"
m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
m.SetFrameReceptionCallback(frame_callback)

while not m.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

print("Starting")

time.sleep(1)

reset_message()

# open a file for the timings
f = open("timings.txt", "w")
# write the columns to the file: test name; message size; send time; processed time; receive time; total time
f.write("test_name;message_size;send_time;processed_time;receive_time;total_time\n")

# a method to send a message and time the process
def send_message(message, test_name, message_size, num_repetitions = 1) :
  global message_buffer, m
  # repeat the process
  for i in range(num_repetitions) :
    # reset the message buffer
    reset_message()
    # get the current time
    start_time = time.time()
    # send the message
    m.SendMessage(message)
    # get the time after sending
    send_time = time.time()
    # get the message from the buffer
    message = get_message()
    # get the time after receiving
    receive_time = time.time()
    # extract the time from the message
    processed_time = message["processed_time"]
    # calculate the total time
    total_time = receive_time - start_time
    # write the data to the file
    f.write(test_name + ";" + str(message_size) + ";" + str(send_time - start_time) + ";" + str(processed_time - send_time) + ";" + str(receive_time - processed_time) + ";" + str(total_time) + "\n")

# query message
message = {"type": "query"}
send_message(message, "query", len(str(message)))
# query of properties
message = {"type": "query", "object": "BP_SynavisDrone_C_0"}
send_message(message, "query of properties", len(str(message)), 100)
#query of value
message = {"type": "query", "object": "BP_SynavisDrone_C_0", "property": "MaxVelocity"}
send_message(message, "query of value", len(str(message)), 100)
# setting a value
message = {"type": "parameter", "object": "BP_SynavisDrone_C_0", "property": "MaxVelocity", "value": 100}
send_message(message, "setting a value", len(str(message)), 100)

# creation of a geometry, a box with 10m side length
points = np.array([[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0], [0, 0, 10], [10, 0, 10], [10, 10, 10], [0, 10, 10]])
triangles = np.array([[0, 1, 2], [0, 2, 3], [0, 4, 5], [0, 5, 1], [1, 5, 6], [1, 6, 2], [2, 6, 7], [2, 7, 3], [3, 7, 4], [3, 4, 0], [4, 7, 6], [4, 6, 5]])
normals = np.array([[0, 0, -1], [1, 0, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 1, 0]])
message = {"type": "directbase64", "points": base64.b64encode(points), "triangles": base64.b64encode(triangles), "normals": base64.b64encode(normals), "name": "box"}
send_message(message, "creation of a geometry", len(str(message)), 100)
