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
import pandas as pd
import datetime

# if the runtime folder is not the python script folder, switch there
if os.path.dirname(os.path.abspath(__file__)) != os.getcwd():
  os.chdir(os.path.dirname(os.path.abspath(__file__)))

# add Synavis (some parent directory to this) to path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build_unix"))
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build"))
import PySynavis as rtc

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
from functional.Leuning import Leuning

# load MPI environment
from mpi4py import MPI
comm = MPI.COMM_WORLD
MPIRank = comm.Get_rank()
MPISize = comm.Get_size()

field_size = 20
cmd_man = rtc.CommandLineParser(sys.argv)
if cmd_man.HasArgument("fs"):
  field_size = cmd_man.GetArgument("fs")
# endif

class Soil :
  def __init__(self) :
    self.source = pd.read_csv("Soil_parameter.csv")
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
    with open ("SE_EC_001.1711883710395.csv", "r") as f :
      # get line 93
      for i in range(92) :
        f.readline()
      # get the column data
      line = f.readline()[1:]
      line = [c.strip() for c in line.split(",")]
      self.columns = line
    # endwith
    self.data = pd.read_csv("SE_EC_001.1711883710395.csv", skiprows = 93, names = self.columns)
    for col in self.data.columns:
      if "QualityFlag" in col:
        self.data[col] = self.data[col].str.split("_", expand=True)[1]
        # convert the values to numeric
        self.data[col] = pd.to_numeric(self.data[col], errors='coerce')
      # endif
    # endfor
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
    soil_moisture = self(time, "SoilMoisture_10cm_Avg10min [m3*m-3]")
    # water content 0% -> -10kPa, 100% -> -1500kPa
    return -10.0 + soil_moisture * (-1500.0 + 10.0)
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

class Field :
  def __init__(self, Lat, Long, Time, Size, MPIRank, MPISize, spacing = 1.0) :
    # initialize all
    self.Lat = Lat
    self.Long = Long
    self.Time = Time
    self.Size = Size
    self.LocalSize = Size // MPISize
    self.MPIRank = MPIRank
    self.MPISize = MPISize
    # strategy of placement
    self.strategy = "square" # "random"
    # field is always square, so our part depends on the rank
    self.side_length = int(np.floor(np.sqrt(Size)))
    rank_per_side = int(np.floor(np.sqrt(MPISize)))
    self.local_side_length = self.side_length // rank_per_side
    start_i = MPIRank % rank_per_side
    start_j = MPIRank // rank_per_side
    self.field = np.zeros((self.LocalSize, 2))
    for k in range(self.LocalSize) :
      i = k // self.local_side_length
      j = k % self.local_side_length
      self.field[k] = np.array([start_i * self.local_side_length + i, start_j * self.local_side_length + j])
    #endfor
    self.field = self.field.astype(int)
    # with this, create the seed position array
    self.seed_positions = self.field * spacing + [start_i * self.local_side_length * spacing, start_j * self.local_side_length * spacing]
    self.plant_initalized = False
  #enddef
  def get_position_from_local(self, local_id) :
    return self.seed_positions[local_id]
  #enddef
  def get_position_from_global(self, global_id) :
    k = global_id[0] * self.local_side_length + global_id[1]
    return self.get_position_from_local(k)
  #enddef
  def get_global_from_local(self, local_id) :
    i = local_id // self.local_side_length
    j = local_id % self.local_side_length
    return [i, j]
  #enddef
  def global_id_value(self, global_id) :
    # combine two 32 bit integers to one 64 bit integer
    value = global_id[0] << 32 | global_id[1]
    return value
  #enddef
  def init_plants(self, parameter_file) :
    self.plants = [pb.MappedPlant() for i in range(self.LocalSize)]
    self.plant_initalized = True
    for i,p in enumerate(self.plants) :
      p.readParameters(parameter_file)
      sp = p.getOrganRandomParameter(1)[0]
      sp.seedPos = pb.Vector3d(self.get_position_from_local(i)[0], self.get_position_from_local(i)[1], sp.seedPos.z)
      sdf = pb.SDF_PlantBox(np.Inf, np.Inf, 40)
      p.setGeometry(sdf)
      p.initialize(False,True)
    #endfor
  #enddef
  def simulate(self, dt, verbose = False) :
    if not self.plant_initalized :
      raise Exception("Plants not initialized")
    for i,p in enumerate(self.plants) :
      p.simulate(dt, verbose = verbose)
    #endfor
  #enddef
  def get_plant(self, local_id) :
    return self.plants[local_id]
  #enddef
  def get_plants(self) :
    return self.plants
  #enddef
  def send_plant(self, local_id, dataconnector) :
    plant = self.get_plant(local_id)
    vis = pb.PlantVisualiser(plant)
    # get organs
    organs = plant.getOrgans()
    # send all stem or leaf organs
    for organ in organs :
      if organ.organType() == pb.Stem or organ.organType() == pb.Leaf :
        vis.ComputeGeometryForOrgan(organ)
        p,i,n = vis.GetGeometry(), vis.GetGeometryIndices(), vis.GetGeometryNormals()
        t = vis.GetGeometryTextureCoordinates()
        message = {"type":"do"}
        message["p"] = base64.b64encode(p.astype("float64")).decode("utf-8")
        message["i"] = base64.b64encode(i.astype("int32")).decode("utf-8")
        message["n"] = base64.b64encode(n.astype("float64")).decode("utf-8")
        message["t"] = base64.b64encode(t.astype("float64")).decode("utf-8")
        message["l"] = local_id
        message["o"] = int(organ.organType())
        dataconnector.send(message)
      #endif
    #endfor
  #enddef
  def init_photo(self) :
    self.photo = [Leuning(plant) for plant in self.plants]
    for r in self.photo :
      r.setKr([[1.728e-4], [1.e-20], [0.004]])  # gmax will be changed by the leuning function
      r.setKx([[4.32e-1]])
      # leaf_nodes = r.get_nodes_index(4)
    #endfor
  #enddef
  def measureLight(self, local_id, dataconnector) :
    message = {"type":"mm", "l":local_id}
    leaf_nodes = self.photo[local_id].get_nodes_index(pb.leaf)
    leaf_nodes = self.plant[local_id].getNodes()[leaf_nodes]
#endclass

""" Parameters """
kz = 4.32e-1  # axial conductivity [cm^3/day]
kr = 1.728e-4  # radial conductivity of roots [1/day]
kr_stem = 1.e-20  # radial conductivity of stem  [1/day], set to almost 0
gmax = 0.004  #  cm3/day radial conductivity of leaves = stomatal conductivity [1/day]
p_a = -1000  # static air water potential
k_soil = []

# model parameters
simtime = 26 # days
depth = 40 # cm
dt = 0.1 # hours
verbose = False

start_time = datetime.datetime(2021, 6, 21, 13, 0, 0, 0)
end_time = start_time + datetime.timedelta(days = simtime)
timerange_numeric = np.arange(0, simtime, dt)
timerange = [start_time + datetime.timedelta(hours = t) for t in timerange_numeric]


parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "plant", "Triticum_aestivum_adapted_2023.xml")

weather = Weather(55.7, 13.2, start_time)
soil = Soil()
field = Field(55.7, 13.2, start_time, field_size, MPIRank, MPISize, spacing = 10.0)

# Numerical solution
results = []
resultsAn = []
resultsgco2 = []
resultsVc = []
resultsVj = []
resultscics = []
resultsfw = []
resultspl = []

Testing = True

if Testing :
  print("Testing whether the plant inits work...")
  field.init_plants(parameter_file)
  print("Testing whether the initialization worked")
  for i in range(field.LocalSize) :
    print(str(field.get_plant(i).getOrganRandomParameter(1)[0].seedPos))
else :
  field.init_plants(parameter_file)
  dataconnector = rtc.DataConnector()
  dataconnector.SetTakeFirstStep(False)
  for i,t in enumerate(timerange_numeric):
    field.simulate(dt, verbose = False)
    time_string_ue = timerange[i].strftime("%Y.%m.%d-%H:%M:%S")
    dataconnector.SendJson({"type":"t", "s":time_string_ue})
    for l_i in range(field.LocalSize) :
      field.send_plant(l_i, dataconnector)
      Q_input = field.measureLight(l_i, dataconnector)
      RH_input = weather.relative_humidity(timerange[i])
      Tair_input = weather.temperature(timerange[i])
      p_s_input = soil.soil_matric_potential(0.1)
      N_input = 4.4  # nitrogen satisfaction for small wheat plants
      cs_input = weather.molar_fraction_co2(timerange[i])
      var = [Q_input, RH_input, Tair_input, p_s_input, N_input, cs_input]
      es = 0.61078 * np.exp(17.27 * Tair_input / (Tair_input + 237.3))  # FAO56
      ea = es * RH_input
      VPD = es - ea
      field.photo[l_i].Param['Patm'] = weather.air_pressure(timerange[i]) * 100.0 # mbar to Pa
      rx = field.photo[l_i].solve_leuning(sim_time = simtime, sxx = [p_s_input], cells = True, Qlight = Q_input, VPD = VPD, Tl = Tair_input + 273.15, p_linit = p_s_input,
      ci_init = cs_input * 0.7, cs = cs_input, soil_k = [], N = N_input, log = False, verbose = False)
      fluxes = field.photo[l_i].radial_fluxes(simtime, rx, [p_s_input], k_soil, True)  # cm3/day
      organTypes = np.array(field.photo[l_i].rs.organTypes)
      results.append(sum(np.where(organTypes == 4, fluxes, 0)))
      resultsAn.append(np.mean(field.photo[l_i].An) * 1e6)
      resultsVc.append(np.mean(field.photo[l_i].Vc) * 1e6)
      resultsVj.append(np.mean(field.photo[l_i].Vj) * 1e6)
      resultsgco2.append(np.mean(field.photo[l_i].gco2))
      resultscics.append(np.mean(field.photo[l_i].ci) / cs_input)
      resultsfw.append(np.mean(field.photo[l_i].fw))




