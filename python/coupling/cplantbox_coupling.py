import sys
import os
import numpy as np
import base64
import time
import json


sys.path.append("/mnt/g/temp/CPlantBox/")
sys.path.append("/mnt/g/temp/CPlantBox/src/visualisation")
#sys.path.append("C:/work/CPlantBox/build_vs/src/Release")

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
  sys.path.append("../build/synavis/Release/")
else :
  sys.path.append("../unix/")
sys.path.append("../modules/")
import PySynavis as rtc

#make the data connector
dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "172.29.192.1","SignallingPort":8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetDataCallback(data_callback)
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)

while not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

reset_message()

# send a message
dataconnector.SendJSON({"type":"command", "name":"cam", "camera": "scene"})

def SendOrganType(plant, organ_type, name) :
  vis = pb.PlantVisualiser(plant)
  vis.SetGeometryResolution(8)
  vis.SetLeafResolution(30)
  vis.ComputeGeometryForOrganType(organ_type, True)
  points = vis.GetGeometry()
  points = np.array(points)
  triangles = vis.GetGeometryIndices()
  triangles = np.array(triangles)
  triangles = triangles.reshape((int(len(triangles) / 3), 3))
  triangles = np.flip(triangles, 1)
  triangles = triangles.flatten()
  normals = vis.GetGeometryNormals()
  normals = np.array(normals).astype("float32")
  normals = normals.reshape((int(len(normals) / 3), 3))
  normals = normals.flatten()
  texcoords = vis.GetGeometryTextureCoordinates()
  node_ids = vis.GetGeometryNodeIds()
  scalars = vis.GetGeometryNodeIds()
  dataconnector.SendGeometry(points, triangles, normals, name, texcoords, None)

# run forever
while True :
  time.sleep(2)
  # Setting up the simulation
  plant = pb.MappedPlant()
  path = "./coupling/"
  plant.readParameters(path + "P0_plant.xml")
  stime = 30
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
  vis = pb.PlantVisualiser(plant)
  vis.SetGeometryResolution(8)
  vis.SetLeafResolution(leaf_res)
  # Simulate
  plant.simulate(stime, True)
  SendOrganType(plant, pb.leaf, "leaf")
  SendOrganType(plant, pb.stem, "stem")
  SendOrganType(plant, pb.root, "root")


