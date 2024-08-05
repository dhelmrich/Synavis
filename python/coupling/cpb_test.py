import os
import sys
import numpy as np
import base64
import time

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
p.initialize(False, True)
p.simulate(18, False)

v = pb.PlantVisualiser(p)
v.SetLeafResolution(10)
v.SetGeometryResolution(6)
v.SetComputeMidlineInLeaf(False)

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

rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogError)

dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": "172.20.16.1", "SignallingPort": 8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)
dataconnector.LockUntilConnected(500)

dataconnector.SendJSON({"type":"placeplant", 
                        "number": 4,
                        "rule": "square",
                        "mpi_world_size": 1,
                        "mpi_rank": 0,
                        "spacing": 50.0,
                      })
time.sleep(1)

for i in range(4) :
  for o in p.getOrgans() :
    print("Organ ", o.getId(), " of type ", o.organType())
    v.ResetGeometry()
    v.ComputeGeometryForOrgan(o.getId())
    points,triangles,normals = np.array(v.GetGeometry())*100.0, np.array(v.GetGeometryIndices()), np.array(v.GetGeometryNormals())
    dataconnector.SendJSON({"type":"do",
                    "p":base64.b64encode(points.astype("float64")).decode('utf-8'),
                    "n":base64.b64encode(normals.astype("float64")).decode('utf-8'),
                    "i":base64.b64encode(triangles.astype("int32")).decode('utf-8'),
                    "o":o.organType(),
                    "l":i,
                    })
    time.sleep(0.1)

