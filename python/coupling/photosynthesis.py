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

import plantbox as pb

# load MPI environment
from mpi4py import MPI
comm = MPI.COMM_WORLD
MPIRank = comm.Get_rank()
MPISize = comm.Get_size()

class Soil :
  def __init__(self) :
    self.source = pd.read_csv("Soil_parameter.csv")
  #enddef
  def get_pressure_head(self, depth) :
    hydroprop = self.source["Hyprop"]
    measuredepth = self.source["Depth"]
    return np.interp(depth, measuredepth, hydroprop)
  #enddef
#endclass

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
  def __init__(self, Lat, Long, Time, Size, MPIRank, MPISize, density = 1.0) :
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
    self.sidelength = np.floor(np.sqrt(Size)) / np.floor(np.sqrt(MPISize))
    start_p = MPISize % np.floor(np.sqrt(MPISize))
    start_q = np.floor(np.sqrt(MPISize))
    start_i = start_p * self.sidelength
    start_j = start_q * self.sidelength
    # make a map of the field indices
    self.field = np.zeroes((self.sidelength, self.sidelength,2))
    for i in range(self.sidelength) :
      for j in range(self.sidelength) :
        self.field[i,j,0] = start_i + i
        self.field[i,j,1] = start_j + j
      #endfor
    #endfor
    # calculate partition size (p,q) in world units from density and MPI size
    self.partition_size = np.floor(np.sqrt(Size)) / np.floor(np.sqrt(MPISize)) * density
    # with this, create the seed position array
    self.seed_positions = self.field * self.partition_size * np.array([self.p, self.q])
    self.plant_initalized = False
  #enddef
  def get_position_from_local(self, local_id) :
    i = local_id // self.sidelength
    j = local_id % self.sidelength
    return self.seed_positions[i,j]
  #enddef
  def get_position_from_global(self, global_id) :
    return self.field[global_id[0], global_id[1]]
  #enddef
  def get_global_from_local(self, local_id) :
    i = local_id // self.sidelength
    j = local_id % self.sidelength
    return self.field[i,j]
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
      p.setSeed(self.global_id_value(self.get_global_from_local(i)))
      sdf = pb.SDF_PlantBox(np.Inf, np.Inf, 40)
      p.setGeometry(sdf)
      p.initialize()
    #endfor
  #enddef
  def simulate(self, simtime, dt, verbose = False) :
    if not self.plant_initalized :
      raise Exception("Plants not initialized")
    for i,p in enumerate(self.plants) :
      p.simulate(simtime, verbose = verbose)
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
      #endif
    #endfor
  #enddef
#endclass


# add Synavis (some parent directory to this) to path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build_unix"))
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build"))
import PySynavis as rtc

def SendGeometry(plant, dataconnector, organ = -1) :
  # get the geometry from the plant
  vis = pb.PlantVisualiser(plant)
  # get the geometry
  if organ >= 0 :
    vis.computeGeometryForOrgan(organ)
  else :
    vis.computeGeometry()
  p,t,n = vis.GetGeometry(), vis.GetGeometryIndices(), vis.GetGeometryNormals()
  # get the center points
  c = plant.getNodes()
  # send the geometry
  dataconnector.SendGeometry(p, t, n)
  dataconnector.SendFloat32Buffer(c)

# modelparameters
parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "Triticum_aestivum_adapted_2023.xml")

# model parameters
simtime = 26 # days
depth = 40 # cm
dt = 0.1 # hours
verbose = False

weather = Weather(55.7, 13.2, datetime.datetime(2023, 1, 1, 0, 0, 0))
soil = Soil()

plant = pb.MappedPlant(seednum = 2)
plant.readParameters(parameter_file)

sdf = pb.SDF_PlantBox(np.Inf, np.Inf, depth)
plant.setGeometry(sdf)
plant.initialize(verbose = verbose)

plant.simulate(simtime, verbose = verbose)

"""
TairC : air temperature in *C
TairK: air tempreature in *K
Pair : air pressure [hPa]
es: saturation vapour pressure [hPa],
ea: actual vapour pressure [hPa],
Qlight: absorbded photosynthetically active radiation (mol m-2 s-1)
rbl, rcanopy, rair: resistivity to the water vapour flow in , respectivelly, the leaf boundary layer, the canopy, the distance between the canopy and the point of air humidity measurment (assumed to be 2m above ground) [s/m]
cs : molar fraction of CO2 in the air [mol/mol]
RH: relative air humidity [-]
p_mean: mean soil water potential [cm[
vg: soil van genuchten parameters
"""

# raise Exception
""" Coupling to soil """

min_b = [-3. / 2, -12. / 2, -41.]  # distance between wheat plants
max_b = [3. / 2, 12. / 2, 0.]
rez = 0.5
cell_number = [int(6 * rez), int(24 * rez), int(40 * rez)]  # 1cm3?
layers = depth;
soilvolume = (depth / layers) * 3 * 12
k_soil = []
initial =   # mean matric potential [cm] pressure head

p_mean = initial
p_bot = p_mean + depth / 2
p_top = initial - depth / 2
sx = np.linspace(p_top, p_bot, depth)
picker = lambda x, y, z: max(int(np.floor(-z)), -1)
sx_static_bu = sx
plant.setSoilGrid(picker)  # maps segment

""" Parameters phloem and photosynthesis """
r = PhloemFluxPython(pl, psiXylInit=min(sx),
                      ciInit=weather.molar_fraction_co2() * 0.5)  # XylemFluxPython(pl)#
# r2 = PhloemFluxPython(#pl2,psiXylInit = min(sx),ciInit = weatherInit["cs"]*0.5) #XylemFluxPython(pl)#

r = setKrKx_phloem(r)

def resistance2conductance(resistance, r, TairK):
    resistance = resistance * (1 / 100)  # [s/m] * [m/cm] = [s/cm]
    resistance = resistance * r.R_ph * TairK / r.Patm  # [s/cm] * [K] * [hPa cm3 K−1 mmol−1] * [hPa] = [s] * [cm2 mmol−1]
    resistance = resistance * (1000) * (
                1 / 10000)  # [s cm2 mmol−1] * [mmol/mol] * [m2/cm2] = [s m2 mol−1]
    return 1 / resistance

"""
CONSTANTS
"""

r.oldciEq = True
r.g0 = 8e-3
r.VcmaxrefChl1 = 1.1#1.28
r.VcmaxrefChl2 = 4#8.33
r.a1 = 0.6 / 0.4  # 0.7/0.3#0.6/0.4 #ci/(cs - ci) for ci = 0.6*cs
r.a3 = 2
r.alpha =0.4# 0.2#/2
r.theta = 0.6#0.9#/2
r.k_meso = 1e-3  # 1e-4
r.setKrm2([[2e-5]])
r.setKrm1([[10e-2]])  # ([[2.5e-2]])
r.setRhoSucrose([[0.51], [0.65], [0.56]])  # 0.51
# ([[14.4,9.0,0,14.4],[5.,5.],[15.]])
rootFact = 2
r.setRmax_st(
    [[2.4 * rootFact, 1.5 * rootFact, 0.6 * rootFact, 2.4 * rootFact], [2., 2.],
      [8.]])  # 6.0#*6 for roots, *1 for stem, *24/14*1.5 for leaves
# r.setRmax_st([[12,9.0,6.0,12],[5.,5.],[15.]])
r.KMrm = 0.1  # VERY IMPORTANT TO KEEP IT HIGH
# r.exud_k = np.array([2.4e-4])#*10#*(1e-1)
# r.k_gr = 1#0
r.sameVolume_meso_st = False
r.sameVolume_meso_seg = True
r.withInitVal = True
r.initValST = 0.  # 0.6#0.0
r.initValMeso = 0.  # 0.9#0.0
r.beta_loading = 0.6
r.Vmaxloading = 0.05  # mmol/d, needed mean loading rate:  0.3788921068507634
r.Mloading = 0.2
r.Gr_Y = 0.8
r.CSTimin = 0.4
r.surfMeso = 0.0025
r.leafGrowthZone = 2  # cm
r.StemGrowthPerPhytomer = True  #
r.psi_osmo_proto = -10000 * 1.0197  # schopfer2006
r.fwr = 0

r.cs = weather.molar_fraction_co2()

r.expression = 6
r.update_viscosity = True
r.solver = 1
r.atol = 1e-10
r.rtol = 1e-6
# r.doNewtonRaphson = False;r.doOldEq = False
SPAD = 31.0
chl_ = (0.114 * (SPAD ** 2) + 7.39 * SPAD + 10.6) / 10
r.Chl = np.array([chl_])
r.Csoil = 1e-4
hp = max([tempnode[2] for tempnode in r.get_nodes()]) / 100

for t in np.arange(0, simtime, dt):
  TairC = weather.temperature(t)
  QLight = weather.radiation(t)
  Pair = weather.air_pressure(t)
  es = weather.saturated_vapour_pressure(t)
  ea = weather.actual_vapour_pressure(t)
  rbl = weather.rbl(t)

