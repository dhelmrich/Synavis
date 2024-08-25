import time


# MPI Environment
from mpi4py import MPI
comm = MPI.COMM_WORLD
rank = comm.Get_rank()
size = comm.Get_size()

start_time = time.time() if rank == 0 else 0
# distribute start_time to all ranks
start_time = comm.bcast(start_time, root=0)

meter_addition_steps = [10, 40, 50, 100, 300, 500, 1000, 9000]

def write_phase(f, phase) :
  f.write("Phase: %s - %f\n" % (phase, time.time() - start_time))
  f.flush()
#enddef

# output for this rank
f = open("output_%d_%d.txt" % (rank, int(start_time *1000)), "w")

write_phase(f, "Pre-Init")

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

sys.path.append("./build_unix/")

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
import signalling_server as ss
infiniband = ss.get_interface_ip("ib0")

rtc.SetGlobalLogVerbosity(rtc.LogVerbosity.LogError)

dataconnector = rtc.DataConnector()
dataconnector.Initialize()
dataconnector.SetConfig({"SignallingIP": infiniband, "SignallingPort": 8080})
dataconnector.SetTakeFirstStep(True)
dataconnector.StartSignalling()
dataconnector.SetMessageCallback(message_callback)
dataconnector.SetRetryOnErrorResponse(True)
dataconnector.LockUntilConnected(500)

num_plants = 50000
chunk_size = 6000
plant_ids = np.arange(num_plants)

# plant spacing
spacing = 0.1
# plant size
plant_relative_scaling = 10.0

# Scatter plant ids to MPI ranks
plant_ids = comm.scatter(plant_ids, root=0)


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
    self.start_i = MPIRank % rank_per_side
    self.start_j = MPIRank // rank_per_side
    self.field = np.zeros((self.LocalSize, 2))
    for k in range(self.LocalSize) :
      i = k // self.local_side_length
      j = k % self.local_side_length
      self.field[k] = np.array([self.start_i * self.local_side_length + i, self.start_j * self.local_side_length + j])
    #endfor
    self.field = self.field.astype(int)
    # with this, create the seed position array
    self.seed_positions = self.field * spacing + [self.start_i * self.local_side_length * spacing, self.start_j * self.local_side_length * spacing]
    self.measurements = [[] for i in range(self.LocalSize)]
    self.plant_initalized = False
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
    # check 
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
    self.visualisers = [pb.PlantVisualiser(p) for p in self.plants]
    self.sampling = [SamplingState.IDLE for i in range(self.LocalSize)]
    self.plant_initalized = True
    for i,p in enumerate(self.plants) :
      p.readParameters(parameter_file)
      sp = p.getOrganRandomParameter(1)[0]
      sp.seedPos = pb.Vector3d(self.get_position_from_local(i)[0], self.get_position_from_local(i)[1], sp.seedPos.z)
      sdf = pb.SDF_PlantBox(np.inf, np.inf, 40)
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
  def send_plant(self, local_id, dataconnector, plant = None) :
    if plant is None :
      plant = self.get_plant(local_id)
    vis = pb.PlantVisualiser(plant)
    vis.SetLeafResolution(20)
    vis.SetGeometryResolution(6)
    vis.SetComputeMidlineInLeaf(False)
    vis.SetLeafMinimumWidth(1.0)
    vis.SetRightPenalty(0.5)
    vis.SetVerbose(False)
    # get organs
    organs = plant.getOrgans()
    slot = 0
    dataconnector.SendJSON({"type":"reset", "l":local_id})
    time.sleep(0.1)
    leaf_points = 0
    leaf_amount = 0
    # send all stem or leaf organs
    for organ in organs :
      if organ.organType() != 2 :
        vis.ResetGeometry()
        vis.ComputeGeometryForOrgan(organ.getId())
        points,triangles,normals = np.array(vis.GetGeometry())*plant_relative_scaling, np.array(vis.GetGeometryIndices()), np.array(vis.GetGeometryNormals())
        points += self.get_position_from_local(local_id)
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
        dataconnector.SendJSON(message)
        time.sleep(0.05)
        slot += 1
      #endif
    #endfor
    pylog.log("Sent plant " + str(local_id) + " with " + str(leaf_amount) + " leaf organs and " + str(leaf_points) + " avg points")
  #enddef
  def numLeafnodes(self, local_id) :
    plant = self.get_plant(local_id)
    length = 0
    for o in plant.getOrgans() :
      if o.organType() == 4 :
        length += len(o.getNodes())
      #endif
    return length
  #enddef
  def measureLight(self, local_id, dataconnector, meter_number) :
    message = {"type":"pms"}
    total_num = np.sum([self.numLeafnodes(i) for i in range(self.LocalSize)])
    leaf_nodes = np.zeros((total_num, 3))
    idx = 0
    for plant in self.plants :
      nodes = LeafNodeInformation(plant)[0]
      nodes = np.array(nodes)
      nodes = nodes * plant_relative_scaling
      nodes += self.get_position_from_local(local_id)
      leaf_nodes[idx:idx+len(nodes)] = nodes
    #endfor
    num_chunks = len(leaf_nodes) // chunk_size + 1
    for i in range(num_chunks) :
      start = i*chunk_size
      end = min((i+1)*chunk_size, len(leaf_nodes))
      message["p"] = base64.b64encode(leaf_nodes[start:end].astype("float64")).decode("utf-8")
      signal_relay.append(Signal({"type":"pms"}, self.light_callback))
      message["l"] = i
      dataconnector.SendJSON(message)
      time.sleep(0.1)
    #endfor
  #enddef
  def reset_sampling_state(self) :
    self.sampling = [SamplingState.IDLE for i in range(self.LocalSize)]
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
