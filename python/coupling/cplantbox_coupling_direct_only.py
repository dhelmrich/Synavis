import sys
import os
import numpy as np
import base64
import time
import json


sys.path.append("/mnt/g/temp/CPlantBox/")
sys.path.append("/mnt/g/temp/CPlantBox/src/visualisation")


try :
  import plantbox as pb
except Exception as e :
  print("Error: Could not import plantbox, please set CPLANTBOX_ROOT or add plantbox to your PYTHONPATH")
  sys.exit(1)

# a function that turns a vector by a angle clockwise around the z axis
def rotate_z(vector, angle) :
  angle = np.radians(angle)
  matrix = np.array([
    [np.cos(angle), -np.sin(angle), 0],
    [np.sin(angle), np.cos(angle), 0],
    [0, 0, 1]
  ])
  return np.dot(matrix, vector)

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
dataconnector.SetConfig({"SignallingIP": "172.29.192.1","SignallingPort":8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetDataCallback(data_callback)
dataconnector.SetMessageCallback(message_callback)

while not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
  time.sleep(0.1)

reset_message()

# send a message
#dataconnector.SendJSON({"type":"parameter", "object":"BP_SynavisDrone_C_1", "property":"MaxVelocity", "value": 100.0})
#dataconnector.SendJSON({"type":"parameter", "object":"BP_SynavisDrone_C_1", "property":"DistanceToLandscape", "value": 100.0})
dataconnector.SendJSON({"type":"settings", "settings": {
  "DistanceScale": 2000,
  "BlackDistance": 500
}})
#dataconnector.SendJSON({"type":"command", "name":"cam", "camera": "scene"})

time.sleep(1)

# run forever
while True :
  #time.sleep(1)
  if not dataconnector.GetState() == rtc.EConnectionState.CONNECTED:
    print("Connection lost")
    break
  # Setting up the simulation
  plant = pb.MappedPlant()
  vis = pb.PlantVisualiser(plant)
  path = "./coupling/"
  plant.readParameters(path + "P0_plant.xml")
  stime = 10
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
  vis.SetGeometryResolution(8)
  vis.SetLeafResolution(leaf_res)
  # Simulate
  plant.simulate(stime, True)

  vis.ResetGeometry()
  vis.ComputeGeometryForOrganType(pb.stem, False)
  vis.ComputeGeometryForOrganType(pb.leaf, False)

  # fetch point data
  points = vis.GetGeometry()
  points = np.array(points)
  points = points.reshape((int(len(points) / 3), 3))

  # random per-instance angle
  angle = np.random.uniform(0, 360) / 180. * np.pi
  # rotate the points
  points = np.array([rotate_z(p, angle) for p in points])

  # convert to cm
  points *= 10 # convert to cm
  # increase the z coordinate by 10cm
  points[:, 2] += 10
  # add a spread in the xy plane of 100cm
  x_spread = np.random.uniform(-800, 400)
  y_spread = np.random.uniform(-400, 800)
  points[:, 0] += x_spread
  points[:, 1] += y_spread
  # get the flat array again for base64 encoding
  points = points.flatten()

  print("Number of points: ", len(points))

  # fetch triangle data
  triangles = vis.GetGeometryIndices()
  triangles = np.array(triangles)
  triangles = triangles.reshape((int(len(triangles) / 3), 3))
  # first 10 triangles printed
  print("Triangles: ", triangles[:10])
  # change the order of the triangles
  triangles = np.flip(triangles, 1)
  # get the flat array again for base64 encoding
  triangles = triangles.flatten()
  print("Number of triangles: ", len(triangles))
  # fetch normals
  normals = vis.GetGeometryNormals()

  texcoords = vis.GetGeometryTextureCoordinates()
  node_ids = vis.GetGeometryNodeIds()
  scalars = vis.GetGeometryNodeIds()
  # prepare the metadata
  data = {"type": "directbase64"}
  # send the points
  data["points"] = base64.b64encode(np.array(points).astype("float64")).decode("utf-8")
  data["triangles"] = base64.b64encode(np.array(triangles).astype("int32")).decode("utf-8")	
  data["normals"] = base64.b64encode(np.array(normals).astype("float64")).decode("utf-8")	
  data["texcoords"] = base64.b64encode(np.array(texcoords).astype("float64")).decode("utf-8")
  #data["time"] = time

  # data as string
  string_data = json.dumps(data)
  # check length
  if len(string_data) > 1000000 :
    print("Error: Message too long, ", len(string_data), " bytes")
    sys.exit(1)
  # send the metadata
  dataconnector.SendJSON(data)


