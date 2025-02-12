import base64
import os
import sys
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)
warnings.filterwarnings("ignore", category=UserWarning)
import numpy as np
import matplotlib.pyplot as plt
from scipy.integrate import odeint
from scipy import constants
import scipy.stats as stats
import pandas as pd
import datetime
import time
import json
from enum import Enum
# progress bar
from tqdm import tqdm

##################################################################################################
# Initialization of the transport parts of the model
# The transport parts are the xylem and phloem
# The xylem is responsible for the transport of water and nutrients from the roots to the leaves
# The phloem is responsible for the transport of sugars from the leaves to the roots
# The xylem and phloem are connected to the mesophyll and the stem
##################################################################################################

def setKrKx_xylem(TairC, RH,r,kr_l): #inC
  #mg/cm3
  hPa2cm = 1.0197
  dEauPure = (999.83952 + TairC * (16.952577 + TairC * 
      (- 0.0079905127 + TairC * (- 0.000046241757 + TairC * 
      (0.00000010584601 + TairC * (- 0.00000000028103006)))))) /  (1 + 0.016887236 * TairC)
  siPhi = (30 - TairC) / (91 + TairC)
  siEnne=0
  mu =  pow(10, (- 0.114 + (siPhi * (1.1 + 43.1 * pow(siEnne, 1.25) )))) 
  mu = mu /(24*60*60)/100/1000; #//mPa s to hPa d, 1.11837e-10 hPa d for pure water at 293.15K
  mu = mu * hPa2cm #hPa d to cmh2o d 

  #number of vascular bundles
  VascBundle_leaf = 32
  VascBundle_stem = 52
  VascBundle_root = 1 #valid for all root type
          
  #radius of xylem type^4 * number per bundle
  rad_x_l_1   = (0.0015 **4) * 2; rad_x_l_2   = (0.0005 **4) * 2   
  rad_x_s_1   = (0.0017 **4) * 3; rad_x_s_2   = (0.0008 **4) * 1     
  rad_x_r0_1  = (0.0015 **4) * 4    
  rad_x_r12_1 = (0.00041**4) * 4; rad_x_r12_2 = (0.00087**4) * 1
  rad_x_r3_1  = (0.00068**4) * 1      

  # axial conductivity [cm^3/day]  
  betaXylX =1# 0.1      
  kz_l  = VascBundle_leaf *(rad_x_l_1 + rad_x_l_2)    *np.pi /(mu * 8)  * betaXylX
  kz_s  = VascBundle_stem *(rad_x_s_1 + rad_x_s_2)    *np.pi /(mu * 8) * betaXylX 
  kz_r0 = VascBundle_root * rad_x_r0_1                *np.pi /(mu * 8) * betaXylX  
  kz_r1 = VascBundle_root *(rad_x_r12_1 + rad_x_r12_2)*np.pi /(mu * 8)  * betaXylX
  kz_r2 = VascBundle_root *(rad_x_r12_1 + rad_x_r12_2)*np.pi /(mu * 8)  * betaXylX 
  kz_r3 = VascBundle_root * rad_x_r3_1                *np.pi /(mu * 8) * betaXylX

  #radial conductivity [1/day],0.00014 #
  betaXyl = 1#0.1#0.1
  #kr_l  = 3.83e-5 * hPa2cm * betaXyl# init: 3.83e-4 cm/d/hPa
  kr_s  = 0.#1.e-20  * hPa2cm # set to almost 0
  kr_r0 =6.37e-5 * hPa2cm * betaXyl
  kr_r1 =7.9e-5  * hPa2cm * betaXyl
  kr_r2 =7.9e-5  * hPa2cm * betaXyl
  kr_r3 =6.8e-5  * hPa2cm * betaXyl
  l_kr = 0.8 #cm
  r.setKr([[kr_r0],[kr_s],[kr_l]]) 
  #r.setKr_meso([kr_l]) 
  r.setKx([[kz_r0],[kz_s],[kz_l]])
  
  
  Rgaz=8.314 #J K-1 mol-1 = cm^3*MPa/K/mol
  rho_h2o = dEauPure/1000#g/cm3
  Mh2o = 18.05 #g/mol
  MPa2hPa = 10000
  hPa2cm = 1/0.9806806
  #log(-) * (cm^3*MPa/K/mol) * (K) *(g/cm3)/ (g/mol) * (hPa/MPa) * (cm/hPa) =  cm                      
  #p_a = np.log(RH) * Rgaz * rho_h2o * (TairC + 273.15)/Mh2o * MPa2hPa * hPa2cm
  #done withint solve photosynthesis
  #r.psi_air = p_a #*MPa2hPa #used only with xylem
  return r

def setKrKx_phloem(r): #inC
  #number of vascular bundles
  VascBundle_leaf = 32
  VascBundle_stem = 52
  VascBundle_root = 1 #valid for all root type
  #numPerBundle
  numL = 18
  numS = 21
  numr0 = 33
  numr1 = 25
  numr2 = 25
  numr3 = 1
  #radius of phloem type^4 * number per bundle
  rad_s_l   = numL* (0.00025 **4)# * 2; rad_x_l_2   = (0.0005 **4) * 2   
  rad_s_s   = numS *(0.00019 **4) #* 3; rad_x_s_2   = (0.0008 **4) * 1     
  rad_s_r0  = numr0 *(0.00039 **4) #* 4    
  rad_s_r12 = numr1*(0.00035**4) #* 4; rad_x_r12_2 = (0.00087**4) * 1
  rad_s_r3  = numr3 *(0.00068**4) #* 1      
  # axial conductivity [cm^3/day] , mu is added later as it evolves with CST  
  beta = 0.9 #Thompson 2003a
  kz_l   = VascBundle_leaf * rad_s_l   * np.pi /8 * beta  
  kz_s   = VascBundle_stem * rad_s_s   * np.pi /8 * beta
  kz_r0  = VascBundle_root * rad_s_r0  * np.pi /8 * beta
  kz_r12 = VascBundle_root * rad_s_r12 * np.pi /8 * beta
  kz_r3  = VascBundle_root * rad_s_r3  * np.pi /8 * beta
  kr_l  = 0.#3.83e-4 * hPa2cm# init: 3.83e-4 cm/d/hPa
  kr_s  = 0.#1.e-20  * hPa2cm # set to almost 0
  kr_r0 = 5e-2
  kr_r1 = 5e-2
  kr_r2 = 5e-2
  kr_r3 = 5e-2
  l_kr = 0.8 #0.8 #cm
  r.setKr([[kr_r0,kr_r1 ,kr_r2 ,kr_r0],[kr_s,kr_s ],[kr_l]] , kr_length_= l_kr)
  r.setKx([[kz_r0,kz_r12,kz_r12,kz_r0],[kz_s,kz_s ],[kz_l]])
  a_ST = [[0.00039,0.00035,0.00035,0.00039 ],[0.00019,0.00019],[0.00025]]
  Across_s_l   = numL*VascBundle_leaf *(a_ST[2][0]**2)*np.pi# (0.00025 **2)# * 2; rad_x_l_2   = (0.0005 **4) * 2   
  Across_s_s   = numS *VascBundle_stem * (a_ST[1][0]**2)*np.pi#(0.00019 **2) #* 3; rad_x_s_2   = (0.0008 **4) * 1     
  Across_s_r0  = numr0 *VascBundle_root * (a_ST[0][0]**2)*np.pi#(0.00039 **2) #* 4    
  Across_s_r12 = numr1*VascBundle_root * (a_ST[0][1]**2)*np.pi#(0.00035**2) #* 4; rad_x_r12_2 = (0.00087**4) * 1
  Across_s_r3  =  numr3 *VascBundle_root *(a_ST[0][2]**2)*np.pi# (0.00068**2) #* 1    
  Perimeter_s_l   = numL*VascBundle_leaf *(a_ST[2][0])* 2 * np.pi# (0.00025 **2)# * 2; rad_x_l_2   = (0.0005 **4) * 2   
  Perimeter_s_s   = numS *VascBundle_stem * (a_ST[1][0])* 2 * np.pi#(0.00019 **2) #* 3; rad_x_s_2   = (0.0008 **4) * 1     
  Perimeter_s_r0  = numr0 *VascBundle_root * (a_ST[0][0])* 2 * np.pi#(0.00039 **2) #* 4    
  Perimeter_s_r12 = numr1*VascBundle_root * (a_ST[0][1])* 2 * np.pi#(0.00035**2) #* 4; rad_x_r12_2 = (0.00087**4) * 1
  Perimeter_s_r3  =  numr3 *VascBundle_root *(a_ST[0][2])* 2 * np.pi# (0.00068**2) #* 1  
  r.setAcross_st([[Across_s_r0,Across_s_r12,Across_s_r12,Across_s_r0],[Across_s_s,Across_s_s],[Across_s_l]])
  return r

# if the runtime folder is not the python script folder, switch there
if os.path.dirname(os.path.abspath(__file__)) != os.getcwd():
  os.chdir(os.path.dirname(os.path.abspath(__file__)))

##################################################################################################
# Initialization of the soil and weather parts of the model
# From the Selhausen below-ground experiment, we take the soil classification, particularly
# matric potential. The weather data is takes from a nearby weather station.
# This implementation is an implementation layer above the pandas dataframe, offering unit
# conversion and interpolation.
##################################################################################################

class Soil :
  def __init__(self) :
    self.source = pd.read_csv("./Soil_parameter.csv")
  #enddef
  def get_pressure_head(self, depth) :
    hydroprop = self.source["Hyprop"]
    measuredepth = self.source["depth"]
    return np.interp(depth, measuredepth, hydroprop)
  #enddef
  def soil_matric_potential(self, depth) :
    return self.get_pressure_head(depth)
  #enddef
#endclass


class Weather :
  def __init__(self, Lat, Long, Time ) :
    self.Lat = Lat
    self.Long = Long
    self.Time = Time
    self.Day = self.Time.day
    with open ("SE_EC_001.1730102615105.csv", "r") as f :
      # get line 93
      for i in range(92) :
        f.readline()
      # get the column data
      line = f.readline()[1:]
      line = [c.strip() for c in line.split(",")]
      self.columns = line
      # check if the data has as many columns as expected
      dataline = f.readline()
      dataline = [c.strip() for c in dataline.split(",")]
      if len(dataline) > len(self.columns) :
        self.columns = self.columns + ["feature" + str(i) for i in range(len(dataline) - len(self.columns))]
    # endwith
    self.data = pd.read_csv("SE_EC_001.1730102615105.csv", skiprows = 93, names = self.columns)
    #for col in self.data.columns:
    #  if "QualityFlag" in col:
    #    self.data[col] = self.data[col].str.split("_", expand=True)[1]
    #    # convert the values to numeric
    #    self.data[col] = pd.to_numeric(self.data[col], errors='coerce')
    #  # endif
    ## endfor
    # exchange "noData" with NaN
    self.data = self.data.replace("noData", np.nan)
    # convert the time to datetime
    self.data["Time"] = pd.to_datetime(self.data["Time"], format="%Y-%m-%dT%H:%M:%S%z")
    self.data = self.data.set_index("Time")
    self.quality_flags = self.data.filter(like="QualityFlag")
    self.data = self.data.drop(columns = self.quality_flags.columns)
    self.data = self.data.drop(columns="feature")
    self.data = self.data.apply(pd.to_numeric, errors='coerce')
    self.data.sort_index(inplace=True)
    self.data.index = pd.to_datetime(self.data.index, format="%Y-%m-%dT%H:%M:%S%z", utc=True)
    self.quality_flags.index = self.data.index
    self.data = self.data.asfreq("10T")
    self.quality_flags = self.quality_flags.asfreq("10T")
  #enddef
  def fill_nans(self) :
    self.data = self.data.interpolate(method="time")
  #enddef
  def __call__(self, tp, column) :
    # get the closest time point
    timest = pd.Timestamp(tp, tz="UTC")
    timed = self.data.index.get_indexer([timest], method="nearest")
    return self.data[column][timed].values[0]
  #enddef
  def print_columns(self) :
    print(self.data.columns)
  #enddef
  def relative_humidity(self, time) :
    absolute_humidity = self.__call__(time, "AirHumidity_2m_Avg10min [g*m-3]")
    temperature = self.__call__(time, "AirTemperature_2m_Avg10min_Sensor1 [°C]")
    # calculate the relative humidity
    es = 6.1078 * 10 ** (7.5 * temperature / (237.3 + temperature))
    return absolute_humidity / es
  #enddef
  def wind_speed(self, time) :
    return self.__call__(time, "WindSpeed_10m_Avg10min [m/s]")
  #enddef
  def wind_direction(self, time) :
    return self.__call__(time, "WindDirection_2.52m_Avg10min [°N]")
  #enddef
  def air_pressure(self, time) :
    return self.__call__(time, "AirPressure_1m_Avg10min [mbar]")
  #enddef
  def radiation_watts(self, time) :
    return self.__call__(time, "RadiationGlobal_Avg10min [W*m-2]")
  #enddef
  def radiation_lux(self, time) :
    irradiance = self.__call__(time, "RadiationGlobal_Avg10min [W*m-2]")
    Eqf = lambda l, I: 10 * constants.pi * I * l * constants.nano / (constants.h * constants.c * constants.Avogadro * constants.micro) # [umol/m2s = muE]
    # conversion to UE Lux
    lux = Eqf(555, irradiance) # [lux]
    lux = max(1e-7, lux)
    return lux
  #enddef
  def radiation_par(self, time) :
    photo_radiation = self.__call__(time, "RadiationPhotosyntheticActive_2m_Avg10min [umol*m-2*s-1]")
    photo_radiation = max(1e-7, photo_radiation)
    # umol to mol
    return photo_radiation
  def precipitation(self, time) :
    return self.__call__(time, "Precipitation_Avg10min [mm]")
  #enddef
  def temperature(self, time) :
    return self.__call__(time, "AirTemperature_2m_Avg10min_Sensor1 [°C]")
  #enddef
  def temperature_k(self, time) :
    return self.__call__(time, "AirTemperature_2m_Avg10min_Sensor1 [°C]") + 273.15
  #enddef
  def saturated_vapour_pressure(self, time) :
    temperature = self.__call__(time, "AirTemperature_2m_Avg30min [°C]")
    e0 = 6.1078
    L = 2.5 * 10 ** 6
    R0 = 461.5
    return e0 * np.exp(L/R0 * (1.0/273.15 - 1.0/(temperature + 273.15)))
  #enddef
  def actual_vapour_pressure(self, time) :
    relative_humidity = self.relative_humidity(time)
    saturated_vapour_pressure = self.saturated_vapour_pressure(time)
    return relative_humidity * saturated_vapour_pressure
  #enddef
  def mean_metric_potential(self, time) :
    soil_moisture = self.__call__(time, "SoilMoisture_10cm_Avg10min [m3*m-3]")
    # water content 0% -> -10kPa, 100% -> -1500kPa
    return -10.0 + soil_moisture * (-1500.0 + 10.0)
  #enddef
  def molar_fraction_co2(self, time) :
    co2_molar = self.__call__(time, "AirConcentration_CO2_2m_Avg30min [mmol*m-3]")
    air_pressure = self.air_pressure(time)
    air_molar = (air_pressure * 100.0) / (8.314 * self.temperature_k(time)) * 1000.0
    return co2_molar / air_molar
  #enddef
  def get_time(self) :
    return self.data.index
  #enddef
  def get_columns(self) :
    return self.data.columns
  #enddef
  def rbl(self, time) :
    leaf_thickness = 0.0001 # m
    diffusivity = 2.5 * 10 ** -5
    return leaf_thickness / diffusivity
  #enddef
  def rcanopy(self, time) :
    # resistivity to water vapour flow in the canopy
    eddy_covariance_canopy = 0.1 # m2 s-1
    return 1.0 / eddy_covariance_canopy
  #enddef
  def generator_t(self, property, start_t, dt, end_time) :
    t = start_t
    while t < end_time :
      p = self.__call__(t, property)
      yield t, p
      t += dt
    #endwhile
  #enddef
#endclass

soil = Soil()

##################################################################################################
# Initialization of the Synavis framework to communicate with the Unreal Engine
# The Synavis framework is a Python library that allows communication with the Unreal Engine
# In this instance, this is a coupling that uses only the WebRTC data channel to send and receive
# data from the Unreal Engine. The data is sent as JSON objects.
##################################################################################################

# if the runtime folder is not the python script folder, switch there
if os.path.dirname(os.path.abspath(__file__)) != os.getcwd():
  os.chdir(os.path.dirname(os.path.abspath(__file__)))

# add Synavis (some parent directory to this) to path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "unix"))
import PySynavis as syn
syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogError)
pylog = syn.Logger()
pylog.setidentity("Chamber Experiment")
# make a stand-in for print
def logfun(*args) :
  pylog.log(" ".join([str(a) for a in args]))
#enddef

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "modules"))
import signalling_server as ss

cplantbox_dir = ""

# check if we have a CPLANTBOX_PATH in the environment
if "CPLANTBOX_PATH" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_PATH"])
  sys.path.append(os.environ["CPLANTBOX_PATH"] + "/src")
  cplantbox_dir = os.environ["CPLANTBOX_PATH"]
else:
  # request input for the path to the cplantbox directory
  print("Please select the path to the cplantbox directory")
  folder_selected = input("Path: ")
  if not os.path.isdir(folder_selected):
    print("The path does not exist")
    sys.exit(1)
  # append the path to the sys path
  sys.path.append(folder_selected)
  # write the export line to .bashrc
  with open(os.path.expanduser("~/.bashrc"), "a") as f:
    f.write("export CPLANTBOX_PATH=" + folder_selected + "\n")
  cplantbox_dir = folder_selected
  print("CPLANTBOX_PATH exported to ~/.bashrc")
  print("Please restart the terminal to update the environment variables")
  print("The path has been added to the sys path for this session")
#endif

import plantbox as pb
from functional.xylem_flux import XylemFluxPython
from functional.photosynthesis_cpp import PhotosynthesisPython as Photosynthesis

##################################################################################################
# Initialization of the simulation parameters, particularly the spacetime discretization
# simtime is the total simulation time in days
# depth is the depth of the soil in cm
# dt is the time step in days
# spacing is the spacing between the plants in cm
# SPAD is an acronym for Soil Plant Analysis Development, a measure of chlorophyll content
##################################################################################################

# model parameters
simtime = 0.58333333333 # days
depth = 200 # cm
# dt = 0.1 hours to days
dt = 0.5 / 24.0
#dt = 2.0 / 24.0
spacing = 25
SPAD= 56.5


class Signal :
  def __init__(self, args, callback) :
    self.args = args
    self.callback = callback
  #enddef
  def __call__(self, msg) :
    self.callback(msg)
  #enddef
  # equal operator
  def __eq__(self, args:dict) :
    return all(self.args[key] == args[key] for key in args if key in self.args)
    # any formulation checks if the key is in the args and if the value is different
    #return not any(self.args[key] != args[key] if key in self.args else True for key in args)
  #enddef
  def __str__(self) :
    return str(self.args)
  #enddef
#endclass

message_buffer = []
signal_relay = []

def reset_message() :
  global message_buffer
  message_buffer = []
#enddef

def get_message() :
  global message_buffer
  while len(message_buffer) == 0 :
    time.sleep(0.1)
  #endwhile
  message = message_buffer.pop(0)
  return message
#enddef

def message_callback(msg) :
  global message_buffer, signal_relay, pylog
  try :
    msg = json.loads(msg)
    if "type" in msg :
      #pylog.log("Message received with parameters " + ", ".join([key + ": " + str(msg[key])[0:10] for key in msg]))
      # ignore certain types
      if msg["type"] == "parameter" :
        return
      handler = next((h for h in signal_relay if h == msg), None)
      #logfun("Handler is ", str(handler), " and not any of ", ",".join([str(h) for h in signal_relay]))
      if not handler is None :
        handler(msg)
        # remove the handler
        signal_relay.remove(handler)
      else :
        pylog.log("Appended to message buffer (now " + str(len(message_buffer)) + " messages)")
        message_buffer.append(msg)
      #endif
    #endif
  except :
    pylog.log("Skipping message of size " + str(len(msg)) + " for failing to convert to JSON")
  #endtry
#enddef

# END OF PREAMBLE ########################################################


start_time = datetime.datetime(2016, 4, 20, 5, 00, 00, 1)
weather = Weather(55.7, 13.2, start_time)
end_time = start_time + datetime.timedelta(days = simtime)
timerange_numeric = np.arange(0, simtime, dt)
delta_time = datetime.timedelta(days = dt)
timerange = [start_time]
while timerange[-1] < end_time:
  timerange.append(timerange[-1] + delta_time)

logfun("Will run thorugh " + str(len(timerange)) + " time points")

parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "plant", "Triticum_aestivum_adapted_2021.xml")

# SETUP SYNAVIS ##########################################################

dataconnector = syn.DataConnector()
dataconnector.Initialize()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetConfig({
    "SignallingIP": "172.21.96.1", #ss.get_interface_ip("ib0"),
    "SignallingPort": 8080
  })
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetRetryOnErrorResponse(True)
dataconnector.LockUntilConnected(500)
#time.sleep(1000)
dataconnector.SendJSON({"type":"delete"})


#dataconnector.SendJSON({"type":"command", "name":"cam", "camera": "scene"})
dataconnector.SendJSON({"type":"console", "command":"t.maxFPS 40"})

dataconnector.SendJSON({"type":"spawnmeter", "number": 40, "calibrate": False})
m_ = {"type":"placeplant", 
                        "number": 1,
                        "rule": "square",
                        "mpi_world_size": 1,
                        "mpi_rank": 0,
                        "spacing": 10,
                      }
dataconnector.SendJSON(m_)



# SETUP CPLANTBOX ########################################################

logfun("Setting up plantbox")

# plot three
# Sowing: 2015-10-26
sowing_time = datetime.datetime(2015, 10, 26, 6, 0, 0, 1)
# Emergence: 2015-11-01
emergence_time = datetime.datetime(2015, 11, 1, 6, 0, 0, 1)
# Growing season: start of march
growing_time = datetime.datetime(2016, 3, 5, 6, 0, 0, 1)
# Tasseling: -
# Flowering: 2016-06-03
flowering_time = datetime.datetime(2016, 6, 3, 6, 0, 0, 1)

# time from sowing until start
time_from_sowing = start_time - growing_time

plant_relative_scaling = 10.0

plant = pb.MappedPlant()
#plant.setSeed(1)
#seednum = 1
plant.readParameters(parameter_file)
#sp = plant.getOrganRandomParameter(1)[0]
#sp.seedPos = pb.Vector3d(0,0,0)
sdf = pb.SDF_PlantBox(np.inf, np.inf, depth)
plant.setGeometry(sdf)
plant.initialize(False, True)
r = Photosynthesis(plant, -500, 360e-6/2)
r.setKr([[1.728e-4], [0], [3.83e-5]])  
r.setKx([[4.32e-1]])
# copy of parameters used in dumux-CPB photosynthesis study (C3, pseudo-wheat)
r.g0 = 8e-6
r.VcmaxrefChl1 =1.28
r.VcmaxrefChl2 = 8.33
chl_ = (0.114 *(SPAD**2)+ 7.39 *SPAD+ 10.6)/10
r.Chl = np.array( [chl_]) 
r.Csoil = 1e-4
r.a1 = 0.6/0.4
r.a3 = 1.5
r.alpha = 0.4 
r.theta = 0.6
#r.fwr = 1e-16
r.fw_cutoff = 0.04072 
r.gm =0.01
#r.sh =5e-4
#r.maxErrLim = 1/100
r.maxLoop = 10000
r.minLoop=900
r.p_lcrit = -15000.0/2.0
#r.psiXylInit = -500
#setKrKx_phloem(r)

r.g_bl = 0.7510560293237751
r.g_canopy = 74.84355244993499
r.g_air = 0.6436895839897938
#r.sh = 4e-4
#r.shmesophyll = 4e-4
#r.fwrmesophyll = 0  # 0.001
#r.p_lcritmesophyll = -100000
#r.gm = 0.05
#r.g0 = 8e-6
#r.alternativeAn = False
#r.limMaxErr = 1 / 100;

#r.KMrm = 0.1
#r.sameVolume_meso_st = False
#r.sameVolume_meso_seg = True
#r.withInitVal = True
#r.initValST = 0.  # 0.6#0.0
#r.initValMeso = 0.  # 0.9#0.0
#r.beta_loading = 0.6
#r.Vmaxloading = 0.05  # mmol/d, needed mean loading rate:  0.3788921068507634
#r.Mloading = 0.2
#r.Gr_Y = 0.8
#r.CSTimin = 0.4
#r.surfMeso = 0.0025
#r.leafGrowthZone = 2  # cm
#r.StemGrowthPerPhytomer = True  #
#r.psi_osmo_proto = -10000 * 1.0197  # schopfer2006
#r.fwr = 0

picker = lambda x, y, z: max(int(np.floor(-z)), -1)
plant.setSoilGrid(picker)

logfun("Pre-simulating until chamber measurement")
# pre-simulating until the day of the gas chamber measurement
delta_time_days = time_from_sowing.days
#delta_time_days = 30
plant.simulate(delta_time_days,False)

has_measured = False
measurement = np.array([])
def ReceiveMeasurement(msg) :
  global has_measured, measurement
  if "i" in msg and msg["type"] == "mm" :
    logfun("Received measurement of light from UE")
    has_measured = True
    mlight = base64.b64decode(msg["i"])
    mlight = np.frombuffer(mlight, dtype=np.float32)
    light = np.nan_to_num(mlight)
    measurement = light
  #endif
#enddef

logfun("Starting simulation")

datafile = open("chamber"+str(time.time())+".csv", "w")
datafile.write("time,rh,tair,par,qlight,qlightstd,flux,fw,gco2\n")

CalibrateUnreal = False

# start the measurement
for t in timerange:
  plant.simulate(dt, False)
  vis = pb.PlantVisualiser(plant)
  vis.SetVerbose(False)
  vis.SetLeafResolution(30)
  vis.SetGeometryResolution(6)
  vis.SetComputeMidlineInLeaf(False)
  #vis.SetLeafMinimumWidth(0.025)
  #vis.SetUseStemRadiusAsMin(True)
  vis.SetRightPenalty(0.9)
  vis.SetShapeFunction(lambda t : 1.0*((1 - t**0.6)**0.3))
  vis.SetLeafWidthScaleFactor(0.66)
  time_string_ue = t.strftime("%Y.%m.%d-%H:%M:%S")
  file_time_string = t.strftime("%Y%m%d%H%M%S")
  logfun("Time: " + time_string_ue)
  # check if we already have a numpy array stored in the file
  if os.path.exists("light_" + file_time_string + ".npy") and False :
    if not os.path.exists("light_" + file_time_string + ".txt") :
      #logfun("Writing measurement to file")
      np.savetxt("light_" + file_time_string + ".txt", measurement)
    #endif
    measurement = np.load("light_" + file_time_string + ".npy")
    #measurement /= 1e6
    #logfun("Loaded measurement from file (avg=",str(np.mean(measurement)), " and std=", str(np.std(measurement)))
  else :
    # get organs
    organs = plant.getOrgans()
    slot = 0
    time.sleep(0.1)
    leaf_points = 0
    leaf_amount = 0
    dataconnector.SendJSON({"type":"resetlights"})
    # send all stem or leaf organs
    for organ in organs :
      if organ.organType() != 2 :
        vis.ResetGeometry()
        vis.ComputeGeometryForOrgan(organ.getId())
        points,triangles,normals = np.array(vis.GetGeometry())*plant_relative_scaling, np.array(vis.GetGeometryIndices()), np.array(vis.GetGeometryNormals())
        if organ.organType() == pb.leaf.value :
          leaf_points += len(points)
          leaf_amount += 1
        #endif
        texture = np.array(vis.GetGeometryTextureCoordinates())
        message = {"type":"do",
        "p": base64.b64encode(points.astype("float64")).decode("utf-8"),
        "n": base64.b64encode(normals.astype("float64")).decode("utf-8"),
        "i": base64.b64encode(triangles.astype("int32")).decode("utf-8"),
        "t": base64.b64encode(texture.astype("float64")).decode("utf-8"),
        "l": 0,
        "s": slot,
        "o": int(organ.organType()),
        }
        dataconnector.SendJSON(message)
        time.sleep(0.05)
        slot += 1
      #endif
    #endfor
    #logfun("Sent " + str(leaf_amount) + " leaf organs with " + str(leaf_points) + " points")
    # surrounding variables
    intensity = weather.radiation_lux(t)      # from callibration (UE! CHECK WITH EXPERIMENT!)
    radiation_par = weather.radiation_par(t)*1.63731 # from experiment!  micromole m-2 s-1
    leaf_nodes = r.get_nodes_index(4)[0:-1]
    #plant_node_ids = field.photo[l_i].get_nodes_index(-1)
    if len(leaf_nodes) == 0 :
      #logfun("No leaf nodes found")
      exit(-1)
    plant_nodes = np.array(plant.getNodes())
    dataconnector.SendJSON({"type":"t", "s":time_string_ue})
    logfun("Intensity: " + str(intensity) + " and PAR: " + str(radiation_par))
    #pylog.logjson({"type":"t", "s":time_string_ue})
    dataconnector.SendJSON({
      "type": "parameter",
      "object": "SunSky_C_1.DirectionalLight",
      "property": "Intensity",
      "value": intensity
    })
    time.sleep(0.4)
    if CalibrateUnreal :
      dataconnector.SendJSON({
        "type":"calibrate",
        "flux": radiation_par
      })
    else :
      dataconnector.SendJSON({
        "type":"calibrate",
        "flux": 1.0
      })
    #endif
    leaf_nodes = plant_nodes[leaf_nodes]*plant_relative_scaling
    message = {"type":"mms", "l":0, "d":1.0,"n":0.2, "p": base64.b64encode(leaf_nodes.tobytes()).decode("utf-8")}
    signal_relay.append(Signal({"type":"mm"}, ReceiveMeasurement))
    #logfun("Registered handler for measurement")
    dataconnector.SendJSON(message)
    #logfun("Sent measurement request, waiting now...")
    while not has_measured :
      time.sleep(0.1)
    has_measured = False
    if not CalibrateUnreal :
      measurement = np.array(measurement) / np.max(measurement)
      measurement = measurement * radiation_par
    else :
      measurement = np.array([min(m, radiation_par) for m in measurement])
    #endif
    measurement = measurement / 1e6 # mmol/m2s -> mol/m2s
    #logfun("Received measurement of avg=",str(np.mean(measurement)), " and std=", str(np.std(measurement)))
    #logfun("Saving measurement to file")
    np.save("light_" + file_time_string, measurement)
    #logfun("Received measurement, starting photosynthesis")
  # simulation data
  #p_s_input = soil.soil_matric_potential(0.1) # soil matric potential, lookup soil water potential [cm ~ pascal]
  p_s_input = np.linspace(-150, -250, depth)
  RH_input = weather.relative_humidity(t) # relative humidity, lookup
  #RH_input = 0.85 # from average historic data for april
  Tair_input = 19.19866943 # air temperature [°C], lookup
  Tair_input = weather.temperature(t)
  kr_l = 3.83e-5
  #r = setKrKx_xylem(Tair_input, RH_input, r, kr_l)
  #p_air_input = 101.7164764 # air pressure, lookup Pa
  cs_input = 360e-6 # CO2 molar fraction, lookup
  es = 6.112 * np.exp((17.67 * Tair_input) / (Tair_input + 243.5))
  ea = es * RH_input
  rh = ea / es
  logfun("Relative humidity " + str(rh) + " and PAR: " + str(radiation_par))
  #Patm = 101.3 * ((293.0 - 0.0065 * depth) / 293.0) ** 5.26 # lookup air pressure at time
  r.vQlight = measurement
  r.Qlight = np.mean(measurement)
  rx = r.solve_photosynthesis(sim_time_ = dt * 24.0, sxx_= p_s_input, cells_= True, ea_ = ea, 
  es_ = es, verbose_ = False, doLog_ = False, TairC_ = Tair_input, outputDir_="./")
  fluxes = np.array(r.An)[np.where(np.array(r.ci)> 0)] * 1e6 # cm3/day
  organTypes = np.array(r.rs.organTypes)
  flux_sum_li = np.mean(fluxes)
  logfun("Sum of fluxes " + str(flux_sum_li))
  logfun("r.fw", np.mean(r.fw))
  logfun("r.gco2", np.mean(r.gco2))
  datafile.write((",".join([
    time_string_ue,
    str(RH_input),
    str(Tair_input),
    str(radiation_par),
    str(np.mean(measurement)),
    str(np.std(measurement)),
    str(flux_sum_li),
    str(np.mean(r.fw)),
    str(np.mean(r.gco2))
  ])) + "\n")
  datafile.flush()
#endfor

dataconnector.SendJSON({"type":"quit"})

logfun("Finished simulation")
