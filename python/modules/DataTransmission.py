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

# a class that handles the data transmission
class DataTransmission :
  def __init__(self, dc : DataConnector) :
    self.dataconnector = dc
    

    

