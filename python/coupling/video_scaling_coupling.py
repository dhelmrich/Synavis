import sys
import numpy as np
import base64

from signalling_server import start_signalling_server
from signalling_server import get_interface_ip as ipq

import pytorch_lightning as pl
from torch.utils.data import DataLoader
from torchvision import transforms

class ImageDataset(pl.LightningDataModule) :
  def __init__ (self, image_list, batch_size, num_workers) :
    super().__init__()
    self.image_list = image_list
    self.batch_size = batch_size
    self.num_workers = num_workers
  #endif
  def setup (self, stage) :
    self.train_dataset = ImageDataset(self.image_list, transform=transforms.Compose([transforms.ToTensor()]))
  #endif
  def train_dataloader (self) :
    return DataLoader(self.train_dataset, batch_size=self.batch_size, num_workers=self.num_workers)
  #endif
#endclass

class ImageBuffer(Cache) :
  def __init__ (self, max_size) :
    super().__init__(max_size)
  #endif
  def add_image (self, image) :
    self.add(image)
  #endif
  def get_image (self, index) :
    # choose image transform by random chance
    image = self.get(index)
    chance = np.random.rand()
    if chance < 0.5 :
      image = transforms.functional.hflip(image)
    elif chance < 0.75 :
      image = transforms.functional.vflip(image)
    elif chance < 0.875 :
      image = transforms.functional.rotate(image, 90)
    elif chance < 0.9375 :
      image = transforms.functional.rotate(image, 180)
    elif chance < 0.96875 :
      image = transforms.functional.rotate(image, 270)
    #endif
    return image
  #endif
#endclass

class ImagePacket :
  # class to store image packets
  def __init__ (self, id, nchunks, meta, image_callback) :
    self.packets = []
    self.id = id
    self.nchunks = nchunks
    self.meta = meta
    self.get_meta()
  #endif
  def get_image (self) :
    return base64.b64decode(self.packet)
  #endif
  def get_id (self) :
    return self.id
  #endif
  def get_nchunks (self) :
    return self.nchunks
  #endif
  def get_meta (self) :
    buffer = base64.b64decode(self.meta)
    # buffer contains: 
    """
    struct
    {
      float fov{ 3.f };
      int width{};
      int height{};
      int id{};
      float pos[3]{ 0.f, 0.f, 0.f };
      float rot[3]{ 0.f, 0.f, 0.f };
    }"""
    self.fov = np.frombuffer(buffer, dtype=np.float32, count=1, offset=0)
    self.width = np.frombuffer(buffer, dtype=np.int32, count=1, offset=4)
    self.height = np.frombuffer(buffer, dtype=np.int32, count=1, offset=8)
    self.id = np.frombuffer(buffer, dtype=np.int32, count=1, offset=12)
    self.pos = np.frombuffer(buffer, dtype=np.float32, count=3, offset=16)
    self.rot = np.frombuffer(buffer, dtype=np.float32, count=3, offset=28)
  #endif
  def complete (self) :
    return len(self.packets) == self.nchunks
  #endif
  def get_image (self) :
    decoded_buffer = base64.b64decode("".join(self.packets))
    as_image = np.frombuffer(decoded_buffer, dtype=np.uint8)
    as_image = as_image.reshape(self.height, self.width, 3)
    return as_image
  #endif
  def add_chunk (self, chunk) :
    self.packets.append(chunk)
    if len(self.packets) == self.nchunks and self.image_callback is not None :
      self.image_callback(self)
    #endif
  #endif
#endclass

class ImageCollector :
  # class to collect freeze frame images
  def __init__ (self) :
    self.image_list = []
    self.image_count = 0
  #endif
  def add_packet (self, packet) :
    fov, width, height, id, pos, rot = self.packagemeta(packet)
    if not id in self.image_list :
      self.image_list.append(id)
      self.image_count += 1
  #endif
  def packagemeta(self, packet) :
    meta = packet["meta"]
    meta = base64.b64encode(meta)
    fov = np.frombuffer(meta, dtype=np.float32, count=1, offset=0)
    width = np.frombuffer(meta, dtype=np.int32, count=1, offset=4)
    height = np.frombuffer(meta, dtype=np.int32, count=1, offset=8)
    id = np.frombuffer(meta, dtype=np.int32, count=1, offset=12)
    pos = np.frombuffer(meta, dtype=np.float32, count=3, offset=16)
    rot = np.frombuffer(meta, dtype=np.float32, count=3, offset=28)
    return fov, width, height, id, pos, rot
  #endif

class PlantSpawner :
  def __init__ (self, field_x, field_y, spacing, pos_zero) :
    self.field_x = field_x
    self.field_y = field_y
    self.plant_seeds = np.random(0, 1000, (field_x, field_y))
    self.spacing = spacing
    self.pos_zero = pos_zero
    self.plant_count = 0
    self.plant_list = []
    self.plant_list.append(self.pos_zero)
    self.plant_count += 1
  #endif
  
  def is_in_our_area(self, x, y) :
    mpi_rank = 0
    if "OMPI_COMM_WORLD_RANK" in os.environ:
      mpi_rank = int(os.environ["OMPI_COMM_WORLD_RANK"])
    mpi_size = 1
    if "OMPI_COMM_WORLD_SIZE" in os.environ:
      mpi_size = int(os.environ["OMPI_COMM_WORLD_SIZE"])
    mpi_threads = 1
    if "OMPI_COMM_WORLD_LOCAL_SIZE" in os.environ:
      mpi_threads = int(os.environ["OMPI_COMM_WORLD_LOCAL_SIZE"])
    mpi_x = mpi_rank % mpi_threads
    mpi_y = mpi_rank // mpi_threads
    mpi_x_size = mpi_size % mpi_threads
    mpi_y_size = mpi_size // mpi_threads
    mpi_x_size += 1
    mpi_y_size += 1
    mpi_x_step = self.field_x // mpi_x_size
    mpi_y_step = self.field_y // mpi_y_size
    mpi_x_start = mpi_x_step * mpi_x
    mpi_y_start = mpi_y_step * mpi_y
    mpi_x_end = mpi_x_start + mpi_x_step
    mpi_y_end = mpi_y_start + mpi_y_step
    if mpi_x == mpi_x_size - 1 :
      mpi_x_end = self.field_x
    if mpi_y == mpi_y_size - 1 :
      mpi_y_end = self.field_y
    if x >= mpi_x_start and x < mpi_x_end and y >= mpi_y_start and y < mpi_y_end :
      return True
    else :
      return False

# check if SYNAVIS_ROOT is set
if "SYNAVIS_ROOT" in os.environ:
  sys.path.append(os.environ["SYNAVIS_ROOT"])
else :
  print("SYNAVIS_ROOT is not set, but I will try to import PySynavis from your PYTHONPATH")
try :
  import PySynavis as rtc
except Exception as e :
  print("Error: Could not import PySynavis, please set SYNAVIS_ROOT or add PySynavis to your PYTHONPATH")
  sys.exit(1)

# check if CPLANTBOX_ROOT is set
if "CPLANTBOX_ROOT" in os.environ:
  sys.path.append(os.environ["CPLANTBOX_ROOT"])
else :
  print("CPLANTBOX_ROOT is not set, but I will try to import plantbox from your PYTHONPATH")
try :
  import plantbox as pb
except Exception as e :
  print("Error: Could not import plantbox, please set CPLANTBOX_ROOT or add plantbox to your PYTHONPATH")
  sys.exit(1)

# a buffer for incoming messages and a flag to indicate if a message is readys
message_buffer = ""
message_ready = False
def message_callback(msg) :
  global message_buffer
  global message_ready
  message_buffer = msg
  message_ready = True

# make the media receiver
m = rtc.MediaReceiver()
m.Initialize()
m.IP = ipq("ib")

 

# get mpi amount of threads per process
mpi_threads = 1
if "OMPI_COMM_WORLD_LOCAL_SIZE" in os.environ:
  mpi_threads = int(os.environ["OMPI_COMM_WORLD_LOCAL_SIZE"])
threadpool = [rtc.WorkerThread() for i in range(mpi_threads)]



