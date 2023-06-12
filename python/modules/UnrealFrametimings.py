import numpy as np
import base64
import time
import sys
import os

# Path: cplantbox_coupling.py
# if we are on windows, the path to the dll is different
if sys.platform == "win32" :
  sys.path.append("../build/synavis/Release/")
else :
  sys.path.append("../../build/")
sys.path.append("../modules/")
import PySynavis as rtc


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

# construct a cube geometry
def cube_geometry(size, center) :
  vertices = np.array([[-size / 2, -size / 2, -size / 2],
                       [-size / 2, -size / 2, size / 2],
                       [-size / 2, size / 2, -size / 2],
                       [-size / 2, size / 2, size / 2],
                       [size / 2, -size / 2, -size / 2],
                       [size / 2, -size / 2, size / 2],
                       [size / 2, size / 2, -size / 2],
                       [size / 2, size / 2, size / 2]])
  vertices += center
  indices = np.array([[0, 1, 3], [0, 3, 2],
                      [0, 4, 5], [0, 5, 1],
                      [0, 2, 6], [0, 6, 4],
                      [7, 5, 4], [7, 4, 6],
                      [7, 6, 2], [7, 2, 3],
                      [7, 3, 1], [7, 1, 5]])
  normals = np.array([[-1, -1, -1],
                      [-1, -1, 1],
                      [-1, 1, -1],
                      [-1, 1, 1],
                      [1, -1, -1],
                      [1, -1, 1],
                      [1, 1, -1],
                      [1, 1, 1]])
  return vertices, indices, normals


# construct sphere geometry
def sphere_geometry(radius, center, resolution : int = 10) :
  # construct a sphere geometry
  # radius: radius of the sphere
  # center: center of the sphere
  # resolution: number of triangles per 2*pi
  # returns: a list of vertices and a list of indices
  vertices = []
  indices = []
  normals = []
  # add the top vertex
  vertices.append(center + np.array([0, 0, radius]))
  # add the bottom vertex
  vertices.append(center + np.array([0, 0, -radius]))
  # add the vertices on the sphere
  for i in range(resolution) :
    for j in range(resolution) :
      theta = 2 * np.pi * i / resolution
      phi = np.pi * (j + 1) / (resolution + 1)
      vertices.append(center + np.array([radius * np.sin(phi) * np.cos(theta),
                                         radius * np.sin(phi) * np.sin(theta),
                                         radius * np.cos(phi)]))
      normals.append(np.array([np.sin(phi) * np.cos(theta), np.sin(phi) * np.sin(theta), np.cos(phi)]))
  # add the indices for the top
  for i in range(resolution) :
    indices.append([0, 2 + i, 2 + (i + 1) % resolution])
  # add the indices for the bottom
  for i in range(resolution) :
    indices.append([1, 2 + resolution * resolution + i, 2 + resolution * resolution + (i + 1) % resolution])
  # add the indices for the sides
  for i in range(resolution) :
    for j in range(resolution) :
      indices.append([2 + i + j * resolution,
                      2 + i + (j + 1) * resolution,
                      2 + (i + 1) % resolution + j * resolution])
      indices.append([2 + (i + 1) % resolution + j * resolution,
                      2 + i + (j + 1) * resolution,
                      2 + (i + 1) % resolution + (j + 1) * resolution])
  return  vertices, indices, normals

m = rtc.MediaReceiver()
m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
m.SetFrameReceptionCallback(frame_callback)
m.StartSignalling()

while not m.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

print("Starting")

time.sleep(1)

reset_message()

# send a message to start the profiling
tracefile = "trace_" + str(int(time.time())) + ".utrace"

print("Sending trace command")
m.SendJSON({"type":"console", "command":"trace.File "+ tracefile})

# set start time
start_time = time.time()
# runtime
runtime = 120
while time.time() < start_time + runtime :
  # sleep for 0.1 seconds
  time.sleep(0.05)
  # make a random position above the ground with at least 100m distance to the origin
  pos = np.random.rand(3) * 100 + np.array([0, 0, 0])
  # make a random radius between 0.1 and 1
  radius = np.random.rand() * 0.9 + 0.1
  # start sending geometries
  print("Sending geometry")
  v, i, n = cube_geometry(radius, pos)
  # flatten the arrays
  v = v.flatten()
  i = i.flatten()
  n = n.flatten()
  m.SendGeometry(v, i, "cube", n, None, None, True)

# send a message to stop the profiling
m.SendJSON({"type":"console", "command":"trace.stop"})
# send a message to quit
m.SendJSON({"type":"console", "command":"quit"})


