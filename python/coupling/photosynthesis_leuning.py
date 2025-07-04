import base64
import os
import sys
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)
warnings.filterwarnings("ignore", category=UserWarning)
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
diverging = LinearSegmentedColormap.from_list('diverging', (
                 (0.000, (0.0, 0.0, 1.000)),
                 (0.250, (0.5, 0.7, 0.9)),
                 (0.500, (0.8, 0.8, 0.8)),
                 (0.750, (0.7,0.5,0.4)),
                 (1.000, (1.0, 0.0, 0.0)))
                  )
diverging.set_under((0.0, 0.0, 1.0))
diverging.set_over((1.0, 0.0, 0.0))
from scipy.integrate import odeint
from scipy import constants
import pandas as pd
import datetime
import time
import json
from enum import Enum
# progress bar
from tqdm import tqdm
import vtk

# if the runtime folder is not the python script folder, switch there
if os.path.dirname(os.path.abspath(__file__)) != os.getcwd():
  os.chdir(os.path.dirname(os.path.abspath(__file__)))

# add Synavis (some parent directory to this) to path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "unix"))
import PySynavis as syn
syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogError)
pylog = syn.Logger()
class NoLog :
  def __init__(self) :
    pass
  #enddef
  def log(self, msg) :
    pass
  #enddef
  def logjson(self, msg) :
    pass
  #enddef
#endclass
#pylog = NoLog()
def logfun(*args) :
  pylog.log(" ".join([str(a) for a in args]))
#enddef

logfun("Initialized the logger")

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
from visualisation.vis_tools import PolydataFromPlantGeometry, WavefrontFromPlantGeometry
from functional.xylem_flux import XylemFluxPython
from functional.photosynthesis_cpp import PhotosynthesisPython as Photosynthesis

# load MPI environment
from mpi4py import MPI
comm = MPI.COMM_WORLD
MPIRank = comm.Get_rank()
MPISize = comm.Get_size()
MPIRank = 0
MPISize = 8

# model parameters
simtime = 1 # days
depth = 80 # cm
plant_relative_scaling = 10.0
# dt = 0.1 hours to days
dt = 1.0 / 24.0
spacing = 0.5 * plant_relative_scaling # cm

headstart = 16 # days
verbose = False

field_size = 8*MPISize
cmd_man = syn.CommandLineParser(sys.argv)
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
    temperature = self.__call__(time, "AirTemperature_2m_Avg30min [°C]")
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
    return self.__call__(time, "AirTemperature_2m_Avg30min [°C]")
  #enddef
  def temperature_k(self, time) :
    return self.__call__(time, "AirTemperature_2m_Avg30min [°C]") + 273.15
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

class SamplingState(Enum) :
  IDLE = 0
  SAMPLE = 1
  FINISHED = 2
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
    self.spacing = spacing
    # strategy of placement
    self.strategy = "square" # "random"
    # field is always square, so our part depends on the rank
    self.side_length = int(np.floor(np.sqrt(Size)))
    rank_per_side = int(np.floor(np.sqrt(MPISize)))
    self.local_side_length = self.side_length // rank_per_side
    start_i = MPIRank % rank_per_side
    start_j = MPIRank // rank_per_side
    self.field = np.zeros((self.LocalSize, 2))
    self.plantmap = [{} for i in range(self.LocalSize)]
    for k in range(self.LocalSize) :
      i = k // self.local_side_length
      j = k % self.local_side_length
      self.field[k] = np.array([start_i * self.local_side_length + i, start_j * self.local_side_length + j])
    #endfor
    self.field = self.field.astype(int)
    # with this, create the seed position array
    self.seed_positions = self.field * spacing + [start_i * self.local_side_length * spacing, start_j * self.local_side_length * spacing]
    self.measurements = [[] for i in range(self.LocalSize)]
    self.plant_initalized = False
    # global light range for color mapping
    self.light_range = [0.0, 1.0]
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
    value = global_id[0] << 20 | global_id[1] << 10 | self.MPIRank
    return value
  #enddef
  def init_plants(self, parameter_file) :
    self.plants = [pb.MappedPlant() for i in range(self.LocalSize)]
    self.visualisers = [pb.PlantVisualiser(p) for p in self.plants]
    self.sampling = [SamplingState.IDLE for i in range(self.LocalSize)]
    self.plant_initalized = True
    for i,p in enumerate(self.plants) :
      p.readParameters(parameter_file)
      sp = p.getOrganRandomParameter(1)[0]
      sp.seedPos = pb.Vector3d(self.get_position_from_local(i)[0], self.get_position_from_local(i)[1], sp.seedPos.z)
      # set stochastic seed based on position
      p.setSeed(int(self.global_id_value(self.get_global_from_local(i))))
      sdf = pb.SDF_PlantBox(np.inf, np.inf, 40)
      p.setGeometry(sdf)
      picker = lambda x, y, z: max(int(np.floor(-z)), -1)
      p.setSoilGrid(picker)
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
    vis.SetLeafResolution(30)
    vis.SetGeometryResolution(6)
    vis.SetComputeMidlineInLeaf(False)
    vis.SetLeafMinimumWidth(0.025)
    vis.SetRightPenalty(0.9)
    vis.SetVerbose(False)
    #vis.SetShapeFunction(lambda t : 1.0*((1 - t**2)**0.5))
    vis.SetShapeFunction(lambda t : 1.0*((1 - t**0.6)**0.3))
    vis.SetLeafWidthScaleFactor(0.5)
    # get organs
    organs = plant.getOrgans()
    slot = 0
    time.sleep(0.1)
    leaf_points = 0
    leaf_amount = 0
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
        "l": local_id,
        "s": slot,
        "o": int(organ.organType()),
        }
        self.plantmap[local_id][organ.getId()] = slot
        logfun("Sending geometry for organ ", slot, "(", organ.getId(), ",",organ.organType(),") of plant ", local_id)
        dataconnector.SendJSON(message)
        time.sleep(0.1)
        slot += 1
      #endif
    #endfor
    pylog.log("Sent plant " + str(local_id) + " with " + str(leaf_amount) + " leaf organs and " + str(leaf_points) + " avg points")
  #enddef
  def init_photo(self) :
    # using default initial psixyl and ci value. 
    # should not have an effect on the outputs normally    
    self.photo = [Photosynthesis(plant, -500, 360e-6/2 # chamber defaults
                    #,psiXylInit=min(sx),ciInit=weatherInit["cs"] * 0.5
                    ) for plant in self.plants]
    for r in self.photo :
      # here we have constent Kr
      # can update it to have decrease of Kr with length
      # of root
      r.setKr([[1.728e-4], [0], [3.83e-5]])  
      r.setKx([[4.32e-1]])
      # copy of parameters used in dumux-CPB photosynthesis study (C3, pseudo-wheat)
      r.g0 = 8e-6
      r.VcmaxrefChl1 =1.28
      r.VcmaxrefChl2 = 8.33
      SPAD= 41.0
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
      r.g_bl = 0.7510560293237751
      r.g_canopy = 74.84355244993499
      r.g_air = 0.6436895839897938
      # leaf_nodes = r.get_nodes_index(4)
    #endfor
  #enddef
  def measureLight(self, local_id, dataconnector) :
    message = {"type":"mms", "l":local_id,"d":1.0,"n":0.1}
    leaf_nodes = self.photo[local_id].get_nodes_index(4)
    if len(leaf_nodes) == 0 :
      self.sampling[local_id] = SamplingState.FINISHED
      return
    plant_nodes = np.array(self.plants[local_id].getNodes())
    leaf_nodes = plant_nodes[leaf_nodes]*plant_relative_scaling
    message["p"] = base64.b64encode(leaf_nodes.astype("float64")).decode("utf-8")
    signal_relay.append(Signal({"type":"mm", "l":local_id}, lambda msg : self.setLight(local_id,msg["i"])))
    pylog.log("Requesting light data for plant: " + str({"type":"mm", "l":local_id}))
    dataconnector.SendJSON(message)
    return len(leaf_nodes)
  #enddef
  def setLight(self, local_id, light) :
    pylog.log("Received light data for plant " + str(local_id))
    light = base64.b64decode(light)
    light = np.frombuffer(light, dtype=np.float32)
    # NaN -> 0
    light = np.nan_to_num(light)
    light[light<0] = 0.0
    self.measurements[local_id] = light
    # update light_range
    self.light_range[0] = min(self.light_range[0], np.min(light))
    self.light_range[1] = max(self.light_range[1], np.max(light))
    self.sampling[local_id] = SamplingState.FINISHED
  #enddef
  def sendLight(self, local_id, dataconnector, x:int = 32) :
    plant = self.get_plant(local_id)
    i = 0
    o = 0
    for organ in plant.getOrgans() :
      if organ.getId() in self.plantmap[local_id] :
        num = organ.getNumberOfNodes()
        light = np.array(self.measurements[local_id][i:i+num])
        # resample light to x points
        light = np.interp(np.linspace(0, num, x), np.arange(num), light)
        colours = np.zeros((x, 3))
        # use divergent colormap from matplotlib
        #light01 = (light - np.min(light)) / (np.max(light) - np.min(light))
        light01 = (light - self.light_range[0]) / (self.light_range[1] - self.light_range[0])
        # put to -100, 100 range
        light100 = light01 * 200 - 100
        cmap = plt.get_cmap("coolwarm")
        for j in range(x) :
          colours[j] = np.array(cmap(light100[j])[0:3])*255
        #endfor
        # colour to BGR
        colours = colours[:,[2,1,0]]
        # append alpha
        colours = np.append(colours, np.ones((x, 1))*255, axis=1)
        data = base64.b64encode(colours.astype(np.uint8).tobytes()).decode("utf-8")
        message = {
          "type":"plantlight",
          "l":local_id,
          "o":self.plantmap[local_id][organ.getId()],
          "data":data,
          "x" : x,
          "y" : 1,
        }
        dataconnector.SendJSON(message)
        logfun("Sending light (",colours.shape,"->",len(data),") with range (",np.min(light),",",np.max(light),") and mean/std (",np.mean(light),",",np.std(light),")")
        time.sleep(0.1)
        i = num - 1
      #endif
    #endfor
  #enddef
  def segment_light_to_organ(self, local_id) :
    result = {}
    plant = self.get_plant(local_id)
    organs = plant.getOrgans()
    leafs = [o for o in organs if o.organType() == pb.leaf.value]
    organs = [o for o in organs if o.organType() != pb.leaf.value]
    i = 0
    for leaf in leafs :
      id = leaf.getId()
      num = leaf.getNumberOfNodes()
      light = np.array(self.measurements[local_id][i:i+num]).mean()
      val = (light - self.light_range[0]) / (self.light_range[1] - self.light_range[0])
      colour = np.array(diverging(val)[0:3])
      logfun("Leaf ", id, " has average light ", light, " -> ", val, " -> ", colour)
      result[id] = colour
      i += num
    #endfor
    for organ in organs :
      if organ.organType() == pb.stem.value :
        id = organ.getId()
        result[id] = np.array([0.0, 0.0, 0.0])
      #endif
    #endfor
    return result
  #enddef
  def exportPlant(self, local_id, with_light = True, added_string = "") :
    #endif
    # steps:
    # 1. get the plant
    # 2. create a visualiser
    # 3. Get the Geometry+Triangles and Match the Node IDs
    # 4. Create a vtkFloatArray for the light matched to the node ID array
    # 5. Create a vtkPolyData with the Geometry and the Light Array
    # 6. Write the vtkPolyData to a file
    plant = self.get_plant(local_id)
    vis = pb.PlantVisualiser(plant)
    vis.SetLeafResolution(30)
    vis.SetGeometryResolution(6)
    vis.SetComputeMidlineInLeaf(False)
    vis.SetLeafMinimumWidth(0.025)
    vis.SetRightPenalty(0.9)
    vis.SetVerbose(False)
    #vis.SetShapeFunction(lambda t : 1.0*((1 - t**2)**0.5))
    vis.SetShapeFunction(lambda t : 1.0*((1 - t**0.6)**0.3))
    vis.SetLeafWidthScaleFactor(0.5)
    # 
    vis.ComputeGeometryForOrganType(4)
    vtkleaf = PolydataFromPlantGeometry(vis)
    #endfor
    if with_light :
      light = self.measurements[local_id]
      leaf_nodes = self.photo[local_id].get_nodes_index(4)
      # map light[i] belongs to leaf_nodes[i]
      # 0 is everywhere except leaf_nodes in plant_nodes
      vtklight = vtk.vtkFloatArray()
      vtklight.SetName("Light")
      vtklight.SetNumberOfComponents(1)
      vtklight.SetNumberOfTuples(vtkleaf.GetNumberOfPoints())
      vertex_node_id = np.array(vis.GetGeometryNodeIds())
      for i in range(vtkleaf.GetNumberOfPoints()) :
        node_id = vertex_node_id[i]
        if node_id in leaf_nodes :
          vtklight.SetValue(i, light[np.where(leaf_nodes == node_id)[0][0]])
        else :
          vtklight.SetValue(i, 0.0)
        #endif
      #endfor
      vtkleaf.GetPointData().AddArray(vtklight)
    #endif
    vis.ResetGeometry()
    WavefrontFromPlantGeometry(vis, "plant_" + str(local_id) + "_" + added_string + ".obj")
    vis.ResetGeometry()
    vis.ComputeGeometryForOrganType(pb.stem.value)
    vtkstem = PolydataFromPlantGeometry(vis)
    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName("stem_" + str(local_id) + "_" + added_string + ".vtp")
    writer.SetInputData(vtkstem)
    writer.Write()
    writer.SetFileName("leaf_" + str(local_id) + "_" + added_string + ".vtp")
    writer.SetInputData(vtkleaf)
    writer.Write()
    vis.ResetGeometry()
    vis.ComputeGeometryForOrganType(pb.root.value)
    #vis.ComputeGeometry()
    vtkroot = PolydataFromPlantGeometry(vis)
    writer.SetFileName("root_" + str(local_id) + "_" + added_string + ".vtp")
    writer.SetInputData(vtkroot)
    writer.Write()
  #enddef
  def exportPlantVideo(self, local_id, added_string = "") :
    plant = self.get_plant(local_id)
    vis = pb.PlantVisualiser(plant)
    vis.SetLeafResolution(30)
    vis.SetGeometryResolution(6)
    vis.SetComputeMidlineInLeaf(False)
    vis.SetLeafMinimumWidth(0.025)
    vis.SetRightPenalty(0.9)
    vis.SetVerbose(False)
    #vis.SetShapeFunction(lambda t : 1.0*((1 - t**2)**0.5))
    vis.SetShapeFunction(lambda t : 1.0*((1 - t**0.6)**0.3))
    vis.SetLeafWidthScaleFactor(0.5)
    colourset = self.segment_light_to_organ(local_id)
    WavefrontFromPlantGeometry(vis, plant, "plant_" + str(local_id) + "_" + added_string + ".obj",
      resolution = "organ",
      colour = colourset,
      fixedset = colourset.keys(),
      add_material_text = "_"+str(local_id)+"_"+added_string)
  #enddef
  def getLight(self, local_id) :
    return self.measurements[local_id]
  #enddef
  def reset_sampling_state(self) :
    self.sampling = [SamplingState.IDLE for i in range(self.LocalSize)]
  #enddef
  def simulate_buffer_zone(self, time) :
    # buffer positions: start_i - 1, start_i + local_side_length, start_j - 1, start_j + local_side_length
    # amount: 4* local_side_length + 4  
    buffer_positions = []
    buffer_plants = []
    for i in range(self.local_side_length) :
      buffer_positions.append([self.start_i - 1, self.start_j + i])
      buffer_positions.append([self.start_i + self.local_side_length, self.start_j + i])
      buffer_positions.append([self.start_i + i, self.start_j - 1])
      buffer_positions.append([self.start_i + i, self.start_j + self.local_side_length])
    #endfor
    for pos in buffer_positions :
      # create a plant
      plant = pb.MappedPlant()
      plant.readParameters(parameter_file)
      sp = plant.getOrganRandomParameter(1)[0]
      sp.seedPos = pb.Vector3d(pos[0] * self.spacing, pos[1] * self.spacing, sp.seedPos.z)
      sdf = pb.SDF_PlantBox(np.inf, np.inf, 40)
      plant.setGeometry(sdf)
      plant.initialize(False,True)
      buffer_plants.append(plant)
    #endfor
    for plant in buffer_plants :
      plant.simulate(time, False)
      self.send_plant(-1, dataconnector, plant)
    #endfor
  #enddef
#endclass

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
      pylog.log("Message received with parameters " + ", ".join([key + ": " + str(msg[key])[0:10] for key in msg]))
      # ignore certain types
      if msg["type"] == "parameter" :
        return
      handler = next((h for h in signal_relay if h == msg), None)
      pylog.log("Handler is " + str(handler))
      if not handler is None :
        handler(msg)
      else :
        pylog.log("Appended to message buffer (now " + str(len(message_buffer)) + " messages)")
        message_buffer.append(msg)
      #endif
    #endif
  except :
    pylog.log("Skipping message of size " + str(len(msg)) + " for failing to convert to JSON")
  #endtry
#enddef


class Reporter :
  def __init__(self, columns, output_graphs = True) :
    self.data = []
    self.output_graphs = output_graphs
    self.columns = columns
  #enddef
  def __call__(self, line) :
    self.data.append(line)
  #enddef
  def write(self, filename, append = False) :
    with open(filename, "a" if append else "w") as f :
      f.write(",".join(self.columns) + "\n")
      for line in self.data :
        f.write(",".join([str(l) for l in line]) + "\n")
      #endfor
    #endwith
  #enddef
#endclass


""" Parameters """
#kz = 4.32e-1  # axial conductivity [cm^3/day]
#kr = 1.728e-4  # radial conductivity of roots [1/day]
#kr_stem = 0  # radial conductivity of stem => 0
k_soil = []

start_time = datetime.datetime(2019, 6, 1, 6, 0, 0, 1)
end_time = start_time + datetime.timedelta(days = simtime)
timerange_numeric = np.arange(0, simtime, dt)
timerange = [start_time + datetime.timedelta(days = t) for t in timerange_numeric]


parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "plant", "Triticum_aestivum_adapted_2023.xml")
#parameter_file = os.path.join(cplantbox_dir, "modelparameter", "structural", "plant", "P3_plant.xml")

weather = Weather(55.7, 13.2, start_time)
weather.fill_nans()
soil = Soil()
field = Field(55.7, 13.2, start_time, field_size, MPIRank, MPISize, spacing = spacing)

#weather.print_columns()
initial_pressure = weather.actual_vapour_pressure(start_time)

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
dataconnector.SendJSON({"type":"delete"})


output = open("output_"+str(int(time.time()*1000))+".csv", "w")
headerstring = "Systime, Timestamp, plant [#], light [Q], Flux [cm3/day], An [umol/m2/s], Vc [umol/m2/s], Vj [umol/m2/s], gco2 [umol/m2/s], cics [-], fw [-]\n"
def rep(row) :
  global output
  output.write(",".join([str(r) for r in row]) + "\n")
  output.flush()

time.sleep(1)

output.write("# field_size: " + str(field_size) + "\n")
output.write("# plant_relative_scaling: " + str(plant_relative_scaling) + "\n")
output.write("# spacing: " + str(spacing) + "\n")
output.write("# simtime: " + str(simtime) + "\n")
output.write("# dt: " + str(dt) + "\n")
output.write("# headstart: " + str(headstart) + "\n")

output.write(headerstring)
output.flush()

time.sleep(1)

dataconnector.SendJSON({"type":"spawnmeter", "number": 20, "calibrate": False, "debug":False})
#pylog.logjson({"type":"console", "command":"t.maxFPS 20"})

m_ = {"type":"placeplant", 
                        "number": field_size,
                        "rule": "square",
                        "mpi_world_size": field.MPISize,
                        "mpi_rank": field.MPIRank,
                        "spacing": field.spacing,
                      }
dataconnector.SendJSON(m_)
pylog.logjson(m_)
#dataconnector.SendJSON({"type":"console", "command":"t.maxFPS 30s"})
#dataconnector.SendJSON({"type":"console", "command":"Log LogTemp off"})
time.sleep(2)

# Numerical solution
results = []
resultsAn = []
resultsgco2 = []
resultsVc = []
resultsVj = []
resultscics = []
resultsfw = []
resultspl = []

Testing = False
ForExport = True
MakePhotosynthesis = True
CalibrateUnreal = False

rep([
            time.time(),
            start_time.strftime("%Y.%m.%d-%H:%M:%S"),
            0,
            0.0,
            0,
            0,
            0,
            0,
            0,
            0,
            0
          ])

if Testing :
  print("Testing whether the plant inits work...")
  field.init_plants(parameter_file)
  print("Testing whether the initialization worked")
  for i in range(field.LocalSize) :
    print(str(field.get_plant(i).getOrganRandomParameter(1)[0].seedPos))
else :
  field.init_plants(parameter_file)
  if headstart > 0.0 :
    field.simulate(headstart, verbose = False)
  field.init_photo()
  #bar = tqdm(total=len(timerange))
  summed_intensity = 0.0
  flux_sum = 0.0
  dataconnector.SendJSON({
    "type": "parameter",
    "object": "SunSky_C_1",
    "property": "Coverage",
    "value": 0.4
  })
  dataconnector.SendJSON({
    "type": "parameter",
    "object": "SunSky_C_1",
    "property": "CloudDensity",
    "value": 0.4
  })
  time.sleep(0.2)
  dataconnector.SendJSON({
    "type": "call",
    "object": "SunSky_C_1",
    "method": "ApplySettings",
  })
  for i,t in enumerate(timerange_numeric):
    # if for Export, re-initialize the plants with offset seeds
    if ForExport :
      field.MPIRank = field.MPIRank + 1
      field.init_plants(parameter_file)
      headstart = headstart + t
      field.simulate(headstart, verbose = False)
    #endif
    time_string_ue = timerange[i].strftime("%Y.%m.%d-%H:%M:%S")
    time_string_file = timerange[i].strftime("%Y%m%d%H%M%S")+str(i)
    intensity = weather.radiation_lux(timerange[i])
    radiation_par = weather.radiation_par(timerange[i])
    dataconnector.SendJSON({"type":"resetlights"})
    field.light_range = [0.25,0.75]
    field.simulate(dt, verbose = False)
    dataconnector.SendJSON({"type":"t", "s":time_string_ue})
    #pylog.logjson({"type":"t", "s":time_string_ue})
    dataconnector.SendJSON({
      "type": "parameter",
      "object": "SunSky_C_1.DirectionalLight",
      "property": "Intensity",
      "value": intensity
    })
    dataconnector.SendJSON({
      "type": "parameter",
      "object": "SunSky_C_1.DirectionalLight",
      "property": "Intensity",
      "value": intensity
    })
    time.sleep(0.2)
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
    floatprint = lambda x: "{:.2f}".format(x)
    pylog.log("Setting intensity to " + str(intensity))
    measured_in_ue = False
    if intensity > 1e-7 and radiation_par > 1e-7 :
      #bar.set_description(time_string_ue + ": {:.2f}lux->UQ:{:.2f}/{:.2f}->{:.4f}mol".format(intensity, summed_intensity*1e6,radiation_par*1e6, flux_sum))
      for l_i in range(field.LocalSize) :
        field.send_plant(l_i, dataconnector)
      time.sleep(0.1)
      for l_i in range(field.LocalSize) :
        field.measureLight(l_i, dataconnector)
      #endfor
      pylog.log("Waiting for all plants to finish sampling")
      while any([s != SamplingState.FINISHED for s in field.sampling]) :
        time.sleep(0.1)
      #endwhile#
      measured_in_ue = True
    else :
      pass
      #bar.set_description(time_string_wdue + ": {:.2f}lux->PY:{:.2f}/{:.2f}->{:.4f}mol".format(intensity, summed_intensity,radiation_par, flux_sum))
    #endif
    pylog.log("All plants finished sampling")
    field.reset_sampling_state()
    summed_intensity = 0.0
    flux_sum = 0.0
    if ForExport :
      for l_i in range(field.LocalSize) :
        #field.exportPlant(l_i, with_light = True, added_string=time_string_file+"b")
        field.exportPlantVideo(l_i, added_string=time_string_file+"_"+str(i))
      #endfor
    elif MakePhotosynthesis :
      for l_i in range(field.LocalSize) :
        # NB: keeping default leaf chlorophyl content of 41 SPAD
        if measured_in_ue :
          Q_input = field.getLight(l_i)[0:-1]
          # to push each measurement to the segment midpoint
          if not CalibrateUnreal :
            # normalize to 1 without moving it to zero
            Q_input = Q_input / np.max(Q_input)
            logfun("Moving range [",np.min(Q_input),",",np.max(Q_input),"] to [",np.min(Q_input)/np.max(Q_input),",1]")
            Q_input = Q_input * radiation_par
          #emdof
        else :
          leaf_node_ids = field.photo[l_i].get_nodes_index(4)[0:-1]
          Q_input = np.zeros(len(leaf_node_ids))
        #endif
        Q_input = np.array(Q_input) / 1e6 # to mol/m2s
        # print the amount of nonzero entries
        #pylog.log("Amount of nonzero entries in light data: " + str(np.count_nonzero(Q_input)) + "/"+str(len(Q_input)))
        Q_ref = np.average(Q_input)
        summed_intensity += Q_ref / field.LocalSize
        RH_input = weather.relative_humidity(timerange[i])
        Tair_input = weather.temperature(timerange[i])
        #p_s_input = soil.soil_matric_potential(0.1)
        p_s_input = np.linspace(-150, -250, depth)
        cs_input = weather.molar_fraction_co2(timerange[i])
        es = 0.61078 * np.exp(17.27 * Tair_input / (Tair_input + 237.3))  # FAO56
        ea = es * RH_input
        field.photo[l_i].Patm = weather.air_pressure(timerange[i]) * 100.0 # mbar to Pa
        field.photo[l_i].cs = cs_input
        if isinstance(Q_input, (float,int)):
          field.photo[l_i].Qlight = Q_input # mean irradiance
        else:
          field.photo[l_i].vQlight = Q_input #  irradiance per leaf segment
        #pylog.log("Computing photosynthesis for plant " + str(l_i) + " with Q=" + str(Q_ref) + " and cs=" + str(cs_input))
        rx = field.photo[l_i].solve_photosynthesis(sim_time_=simtime, sxx_=p_s_input,
                                cells_=True,
                                ea_=ea, es_=es,
                                verbose_=False, doLog_=False, TairC_=Tair_input,
                                outputDir_="./" )
        fluxes = field.photo[l_i].outputFlux  # cm3/day
        organTypes = np.array(field.photo[l_i].rs.organTypes)
        flux_sum_li = np.sum(np.where(organTypes == 4, fluxes, 0))
        flux_sum += flux_sum_li / field.LocalSize
        rep([
              time.time(),
              time_string_ue,
              l_i,
              Q_ref,
              flux_sum_li,
              np.mean(field.photo[l_i].An) * 1e6,
              np.mean(field.photo[l_i].Vc) * 1e6,
              np.mean(field.photo[l_i].Vj) * 1e6,
              np.mean(field.photo[l_i].gco2),
              np.mean(field.photo[l_i].ci) / cs_input,
              np.mean(field.photo[l_i].fw)
            ])
      #endfor
    else :
      for l_i in range(field.LocalSize) :
        field.sendLight(l_i, dataconnector, x=8)
      #endfor
      time.sleep(6)
    #endif
    #bar.update(1)
  #endfor
  #bar.close()
#endif

logfun("Finished simulation")

dataconnector.SendJSON({"type":"quit"})

output.close()


# mpi barrier
comm.barrier()
