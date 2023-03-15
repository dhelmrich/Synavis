
import sys
import os
# check if CPLANTBOX_ROOT is set
if "CPLANTBOX_ROOT" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_ROOT"])
else :
  print("CPLANTBOX_PATH is not set, but I will try to import plantbox from your PYTHONPATH")
try :
  import plantbox as pb
except Exception as e :
  print("Error: Could not import plantbox, please set CPLANTBOX_ROOT or add plantbox to your PYTHONPATH")
  sys.exit(1)

# a buffer for incoming messages and a flag to indicate if a message is readys
message_buffer = ""
message_ready = False

# a callback function for the data connector
def message_callback(msg) :
  global message_buffer
  global message_ready
  message_buffer = msg
  message_ready = True

# a callback function for the data connector
def data_callback(data) :
  print("Received data: ", data)

# Path: cplantbox_coupling.py
import numpy as np
sys.path.append("../../build/")
sys.path.append("../modules/")
import PyWebRTCBridge as rtc
import signalling_server as ss

#make the data connector
dc = rtc.DataConnector()
dc.SetConfig("config.json")
dc.StartSignalling()
dc.SetDataCallback(data_callback)
dc.SetMessageCallback(message_callback)

# run forever
while True :
  # wait for a message
  while not message_ready :
    pass
  # Setting up the simulation
  plant = pb.MappedPlant()
  path = "./results/"
  plant.readParameters(path + "P0_plant.xml")
  time = 10
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
    p.lmax = (time-7)*r
  # Initialize

  plant.initialize()
  plant.SetGeometryResolution(8)
  plant.SetLeafResolution(leaf_res)
  # Simulate
  plant.simulate(time, True)

  # fetch point data
  points = plant.getPoints()
  print("Number of points: ", len(points))
  # fetch triangle data
  triangles = plant.getTriangles()
  print("Number of triangles: ", len(triangles))
  # fetch normals
  normals = plant.getNormals()

  # prepare the metadata
  metadata = {}
  metadata["points"] = len(points)
  metadata["triangles"] = len(triangles)
  metadata["normals"] = len(normals)
  metadata["time"] = time
  metadata["leaf_res"] = leaf_res
  # send the metadata
  dc.SendMessage(metadata)



