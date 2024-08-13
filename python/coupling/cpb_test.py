import os
import sys
import numpy as np
import base64
import time
import json

# check if we have a CPLANTBOX_PATH in the environment
if "CPLANTBOX_PATH" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_PATH"])
  sys.path.append(os.environ["CPLANTBOX_PATH"] + "/src")
  cplantbox_dir = os.environ["CPLANTBOX_PATH"]
else :
  print("Please set the CPLANTBOX_PATH environment variable to the CPLANTBOX installation directory.")
  sys.exit(1)

# import the cplantbox module
import plantbox as pb
from functional.xylem_flux import XylemFluxPython
from functional.Leuning import Leuning

parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "plant", "Triticum_aestivum_adapted_2023.xml")

# create a plant
p = pb.MappedPlant()
p.readParameters(parameter_file)
sp = p.getOrganRandomParameter(1)[0]
p.setSeed(80)
p.initialize(False, False)
p.simulate(18, False)

v = pb.PlantVisualiser(p)
v.SetLeafResolution(20)
v.SetGeometryResolution(6)
v.SetComputeMidlineInLeaf(False)
v.SetLeafMinimumWidth(1.0)
v.SetRightPenalty(0.5)

def LeafNodeInformation(plant) :
  leafs = [o for o in plant.getOrgans() if o.organType() == 4]
  nodes = []
  nodeids = []
  organids = []
  for leaf in leafs :
    leafnodes = leaf.getNodes()
    # move the last node a little closer to the previous node
    if len(leafnodes) > 1 :
      leafnodes[-1].x = leafnodes[-2].x + 0.1*(leafnodes[-1].x - leafnodes[-2].x)
      leafnodes[-1].y = leafnodes[-2].y + 0.1*(leafnodes[-1].y - leafnodes[-2].y)
      leafnodes[-1].z = leafnodes[-2].z + 0.1*(leafnodes[-1].z - leafnodes[-2].z)
    nodes.extend(leafnodes)
    nodeids.extend(leaf.getNodeIds())
    organids.extend([leaf.getId()]*len(leaf.getNodes()))
  return nodes, nodeids, organids

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
  # decode from utf-8
  message_buffer.append(str(msg))
  if "error" in msg.lower() :
    print("Error message: ", msg)

import PySynavis as rtc


rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogError)

dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "172.20.16.1", "SignallingPort": 8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)
dataconnector.LockUntilConnected(500)

intensity = 150000
#intensity = 75000
scale = 10
t_h = 13
t_m = 0
t_s = 0
timestring = "2024.06.01-"+str(t_h).zfill(2)+"."+str(t_m).zfill(2)+"."+str(t_s).zfill(2)

dataconnector.SendJSON({"type":"t", "s": timestring, "rad": intensity})
time.sleep(2)

dataconnector.SendJSON({"type":"placeplant", 
                        "number": 1,
                        "rule": "square",
                        "mpi_world_size": 1,
                        "mpi_rank": 0,
                        "spacing": 50.0,
                      })
dataconnector.SendJSON({"type":"spawnmeter", "number": 30, "calibrate": True})
dataconnector.SendJSON({"type":"console", "command":"t.maxFPS 20"})
#dataconnector.SendJSON({
#  "type": "parameter",
#  "object": "SunSky_C_1.DirectionalLight",
#  "property": "Intensity",
#  "value": float(intensity)
#})
dataconnector.SendJSON({"type":"resetlights"})
time.sleep(2)

organs = p.getOrgans()
organs.sort(key=lambda x: x.organType(), reverse=True)

for o in organs :
  if o.organType() == 2 :
    continue
  print("Organ ", o.getId(), " of type ", o.organType())
  v.ResetGeometry()
  v.ComputeGeometryForOrgan(o.getId())
  points,triangles,normals = np.array(v.GetGeometry())*scale, np.array(v.GetGeometryIndices()), np.array(v.GetGeometryNormals())
  dataconnector.SendJSON({"type":"do",
                  "p":base64.b64encode(points.astype("float64")).decode('utf-8'),
                  "n":base64.b64encode(normals.astype("float64")).decode('utf-8'),
                  "i":base64.b64encode(triangles.astype("int32")).decode('utf-8'),
                  "o":o.organType(),
                  "l":0,
                  })
  time.sleep(0.5)
# end for

nodes, nodeids, organids = LeafNodeInformation(p)
nodes = np.array(nodes)
nodes = nodes*scale
#name = "serial"
name = "parallel"
measurement_time = 8.0
str_mestime = str(int(measurement_time*1000))
str_intensity = str(intensity)
filebase = name+"_"+str_mestime+"_"+str_intensity+"_"+str(int(scale))+str(t_h)+"h"
message = {
  "type":"mms" if name == "parallel" else "mm",
  "l":0,
  "d":measurement_time,
  "p": base64.b64encode(nodes.astype("float64")).decode('utf-8')
}
message = json.dumps(message)
print("We are sending over ", len(nodes), " nodes")
dataconnector.SendJSON(message)

lights = []
while True :
  message = get_message()
  try :
    message = json.loads(message)
    if message["type"] == "mm" or message["type"] == "mms" :
      # extract float array from "i" key
      lights = base64.b64decode(message["i"])
      lights = np.frombuffer(lights, dtype=np.float32)
      print("Received light intensities of size ", len(lights))
      break
  except :
    print("Skipping message of size ", len(message))
  #
#
np.savetxt("nodes_"+filebase+".txt", nodes)
np.savetxt("lights_"+filebase+".txt", lights)
import vtk
# create VTK output
vtkplant = vtk.vtkPolyData()
vtkplant.GetPointData().SetScalars(vtk.vtkFloatArray())
points = vtk.vtkPoints()
vtkplant.SetPoints(points)
lines = vtk.vtkCellArray()
vtkplant.SetLines(lines)
vtklights = vtk.vtkFloatArray()
vtklights.SetName("light")
vtkplant.GetPointData().SetScalars(vtklights)
for k,n in enumerate(nodes) :
  vtkplant.GetPoints().InsertNextPoint(n[0], n[1], n[2])
  if k > 0 and organids[k-1] == organids[k]:
    vtkplant.GetLines().InsertNextCell(2)
    vtkplant.GetLines().InsertCellPoint(k-1)
    vtkplant.GetLines().InsertCellPoint(k)
  vtkplant.GetPointData().GetScalars().InsertNextValue(lights[k])

# write to file
writer = vtk.vtkXMLPolyDataWriter()
writer.SetFileName("plant_"+filebase+".vtp")
writer.SetInputData(vtkplant)
writer.Write()

