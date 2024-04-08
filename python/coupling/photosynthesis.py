import os
import sys
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)
warnings.filterwarnings("ignore", category=UserWarning)
import numpy as np
import matplotlib.pyplot as plt
from scipy.integrate import odeint
form scipy import constants
import pandas as pd
import datetime

cplantbox_dir = ""

# check if we have a CPLANTBOX_PATH in the environment
if "CPLANTBOX_PATH" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_PATH"])
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

class Weather :
  def __init__(self, Lat, Long, Time ) :
    self.Lat = Lat
    self.Long = Long
    self.Time = Time
    self.Day = self.Time.tm_yday
    with open ("SE_EC_001.1711883710395.csv", "r") as f :
      # get line 93
      for i in range(92) :
        f.readline()
      # get the column data
      line = f.readline()[1:]
      line = line.split(",")
      self.columns = line
    # endwith
    self.data = pd.read_csv("SE_EC_001.1711883710395.csv", skiprows = 93, names = self.columns)
    for col in self.data.columns:
    if "QualityFlag" in col:
      self.data[col] = self.data[col].str.split("_", expand=True)[1]
      # convert the values to numeric
      self.data[col] = pd.to_numeric(self.data[col], errors='coerce')
    # endif
    # exchange "noData" with NaN
    self.data = self.data.replace("noData", np.nan)
    # convert the time to datetime
    self.data["Time"] = pd.to_datetime(self.data["Time"], format="%Y-%m-%dT%H:%M:%S%z")
    self.data = self.data.set_index("Time")
    self.quality_flags = self.data.filter(like="QualityFlag")
    self.data = self.data.drop(columns = self.quality_flags.columns)
    self.data = self.data.drop(columns="feature")
    data = data.apply(pd.to_numeric, errors='coerce')
    self.data.sort_index(inplace=True)
    self.data.index = pd.to_datetime(self.data.index, format="%Y-%m-%dT%H:%M:%S%z", utc=True)
    self.quality_flags.index = self.data.index
    self.data = self.data.asfreq("10T")
    self.quality_flags = self.quality_flags.asfreq("10T")
  #enddef
  def fill_nans(self) :
    self.data = self.data.interpolate(method="time")
  #enddef
  def __call__(self, time, column) :
    # get the closest time point
    time = pd.Timestamp(time)
    time = self.data.index.get_loc(time, method="nearest")
    return self.data[column][time]
  #enddef
  def relative_humidity(self, time) :
    absolute_humidity = self(time, "AirHumidity_2m_Avg10min [g*m-3]")
    temperature = self(time, "AirTemperature_2m_Avg10min [degC]")
    # calculate the relative humidity
    es = 6.1078 * 10 ** (7.5 * temperature / (237.3 + temperature))
    return absolute_humidity / es
  #enddef
  def wind_speed(self, time) :
    return self(time, "WindSpeed_10m_Avg10min [m/s]")
  #enddef
  def wind_direction(self, time) :
    return self(time, "WindDirection_2.52m_Avg10min [°N]")
  #enddef
  def air_pressure(self, time) :
    return self(time, "AirPressure_1m_Avg10min [mbar]")
  #enddef
  def radiation(self, time) :
    return self(time, "RadiationGlobal_Avg10min [W*m-2]")
  #enddef
  def precipitation(self, time) :
    return self(time, "Precipitation_Avg10min [mm]")
  #enddef
  def temperature(self, time) :
    return self(time, "AirTemperature_2m_Avg10min [degC]")
  #enddef
  def temperature_k(self, time) :
    return self(time, "AirTemperature_2m_Avg10min [degC]") + 273.15
  #enddef
  def saturated_vapour_pressure(self, time) :
    temperature = self(time, "AirTemperature_2m_Avg10min [degC]")
    return 6.1078 * 10 ** (7.5 * temperature / (237.3 + temperature))
  #enddef
  def actual_vapour_pressure(self, time) :
    relative_humidity = self.relative_humidity(time)
    saturated_vapour_pressure = self.saturated_vapour_pressure(time)
    return relative_humidity * saturated_vapour_pressure
  #enddef
  def molar_fraction_co2(self, time) :
    co2_molar = self(time, "AirConcentration_CO2_2m_Avg30min [mmol*m-3]")
    
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
#endclass


import plantbox as pb

# add Synavis (some parent directory to this) to path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build_unix"))
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build"))
import PySynavis as rtc

# modelparameters
parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "Triticum_aestivum_adapted_2023.xml")

plant = pb.MappedPlant(seednum = 2)
plant.readParameters(parameter_file)


