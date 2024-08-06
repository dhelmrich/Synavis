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
p.initialize(False, True)
p.simulate(18, False)

v = pb.PlantVisualiser(p)
v.SetLeafResolution(10)
v.SetGeometryResolution(6)
v.SetComputeMidlineInLeaf(False)
