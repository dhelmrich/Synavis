import sys
import os
import numpy as np
import base64
import time
import json



# check if CPLANTBOX_ROOT is set
if "CPLANTBOX_ROOT" in os.environ:
  #sys.path.append(os.environ["CPLANTBOX_ROOT"])
  sys.path.append("G:/Work/CPlantBox/build_vs/src/Release/")
else :
  print("CPLANTBOX_ROOT is not set, but I will try to import plantbox from your PYTHONPATH")


try :
  import plantbox as pb
except Exception as e :
  print("Error: Could not import plantbox, please set CPLANTBOX_ROOT or add plantbox to your PYTHONPATH")
  sys.exit(1)

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
  sys.path.append("../build/webrtcbridge/Release/")
else :
  sys.path.append("../../build/")
sys.path.append("../modules/")
import PyWebRTCBridge as rtc

#make the data connector
dataconnector = rtc.DataConnector()
dataconnector.SetConfig({"SignallingIP": "127.0.0.1","SignallingPort":8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetDataCallback(data_callback)
dataconnector.SetMessageCallback(message_callback)



while not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

reset_message()

# run forever
while True :
  time.sleep(2)
  # Setting up the simulation
  plant = pb.MappedPlant()
  path = "./coupling/"
  plant.readParameters(path + "P0_plant.xml")
  stime = 20
  leaf_res = 30
  for p in plant.getOrganRandomParameter(pb.leaf):
    p.lb =  0 # length of leaf stem
    p.la,  p.lmax = 38.41053981, 38.41053981
    p.areaMax = 54.45388021  # cm2, area reached when length = lmax
    phi = np.array([-90,-80, -45, 0., 45, 90]) / 180. * np.pi
    l = np.array([38.41053981,1 ,1, 0.3, 1, 38.41053981]) #distance from leaf center
    p.tropismT = 1 # 6: Anti-gravitropism to gravitropism
    #p.tropismN = 5
    p.tropismS = 0.05
    p.tropismAge = 5 #< age at which tropism switch occures, only used if p.tropismT = 6
    p.createLeafRadialGeometry(phi, l, leaf_res)
  for p in plant.getOrganRandomParameter(pb.stem):
    r= 0.758517633
    p.r = r
    p.lmax = (stime-7)*r
  # Initialize

  plant.initialize()
  plant.SetGeometryResolution(8)
  plant.SetLeafResolution(leaf_res)
  # Simulate
  plant.simulate(stime, True)

  plant.ComputeGeometry()

  # fetch point data
  points = plant.GetGeometry()
  points = np.array(points)
  points = points.reshape((int(len(points) / 3), 3))
  # convert to cm
  points *= 10 # convert to cm
  # increase the z coordinate by 10cm
  points[:, 2] += 10
  # add a spread in the xy plane of 100cm
  x_spread = np.random.uniform(-100, 100)
  y_spread = np.random.uniform(-100, 100)
  points[:, 0] += x_spread
  points[:, 1] += y_spread
  # get the flat array again for base64 encoding
  points = points.flatten()

  print("Number of points: ", len(points))

  # print the first few points to compare with Unreal
  for i in range(0, 10) :
    print(points[i])

  # fetch triangle data
  triangles = plant.GetGeometryIndices()
  triangles = np.array(triangles)
  triangles = triangles.reshape((int(len(triangles) / 3), 3))
  # change the order of the triangles
  triangles = np.flip(triangles, 1)
  # get the flat array again for base64 encoding
  triangles = triangles.flatten()
  print("Number of triangles: ", len(triangles))
  # fetch normals
  normals = plant.GetGeometryNormals()

  texcoords = plant.GetGeometryTextureCoordinates()
  node_ids = plant.GetGeometryNodeIds()
  scalars = plant.GetGeometryNodeIds()
  
  print("Number of normals: ", len(normals))
  print("Number of texcoords: ", len(texcoords))
  print("Number of node_ids: ", len(node_ids))
  print("Sending data to Unreal")
  dataconnector.SendGeometry(points, triangles, normals, "name", texcoords, None)


