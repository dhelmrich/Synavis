import numpy as np
import tensorflow as tf
import tensorflow.keras.backend as K
import horovod.tensorflow.keras as hvd
import cv2
import threading
import time
import sys
import os

# WebRTCBridge: Find build
path = "../../"
# if windows
if os.name == 'nt' :
  path = path + "build/Release/"
else :
  path = path + "build/"
sys.path.append(path)
import WebRTCBridge as rtc

HEIGHT = 512
WIDTH = 512

Media = rtc.MediaReceiver()


BucketSize = 200
Batch = [{
  "img":[],
  "seg":[]
},{
  "img":[],
  "seg":[]
}]
BatchSize = 1
BatchDone = False
BatchID = 0
UseBatch = 0
Halt = False
End = False

def BatchAssembly() :
  global BatchDone,BatchID,socket,BucketSize,End,Halt, datasize,connection_ip, connection_port
  print("Initializing from rtp streaming source")
  file = "udpsrc address=" + connection_ip + " port=" + connection_method + \
    " caps=\"application/x-rtp\" ! queue ! rtph264depay " + \
    "! video/x-h264,stream-format=byte-stream ! queue ! avdec_h264 ! queue ! appsink"
  video = cv2.VideoCapture(file,cv2.CAP_GSTREAMER)
  if not video.isOpened() :
    print("Video capture could not open")
    exit(-1)
  while True :
    ret,frame = video.read()
    if not ret :
      print("Empty frame")
      time.sleep(0.1)
    else :
      output = np.array(frame)
      image = output[int(output.shape[0]/2):,:,:]
      truth = output[:int(output.shape[0]/2),:,:]
      image = cv2.resize(image,NetworkSize)
      truth = cv2.resize(truth,NetworkSize)
      Batch[BatchID]["img"].append(image)
      Batch[BatchID]["seg"].append(truth)
#enddef

class UnrealData(tf.keras.utils.Sequence):
  def __init__(self, batch_size, img_size):
    self.batch_size = batch_size
    self.img_size = img_size
  def __len__(self):
    return BucketSize // self.batch_size
  
  def GetItemNorandom(self,idx) :
    i = idx * self.batch_size
    batch_input_img_paths = Batch[UseBatch]["img"][i : i + self.batch_size]
    batch_target_img_paths = Batch[UseBatch]["seg"][i : i + self.batch_size]
    x = np.zeros((self.batch_size,) + self.img_size + (3,), dtype="float32")
    y = np.zeros((self.batch_size,) + self.img_size + (NetworkOutputDimension,), dtype="float32")
    for j, img in enumerate(batch_input_img_paths):
      yimg = batch_target_img_paths[j]
      x[j] = img
      y[j] = yimg
    return x, y
  
  def __getitem__(self, idx):
    global BatchDone, BatchID, UseBatch, Batch, Halt
    if BatchDone :
      if not Halt :
        Halt = True
        
      print("Noticed that batch is done!")
      t = UseBatch
      UseBatch = BatchID
      BatchID = t
      BatchDone = False
      Batch[BatchID]["img"].clear()
      Batch[BatchID]["seg"].clear()
      Halt = False
    i = idx * self.batch_size
    batch_input_img_paths = Batch[UseBatch]["img"][i : i + self.batch_size]
    batch_target_img_paths = Batch[UseBatch]["seg"][i : i + self.batch_size]
    x = np.zeros((self.batch_size*4,) + self.img_size + (3,), dtype="float32")
    y = np.zeros((self.batch_size*4,) + self.img_size + (NetworkOutputDimension,), dtype="float32")
    for j, img in enumerate(batch_input_img_paths):
      yimg = batch_target_img_paths[j]
      x[j] = img
      y[j] = yimg
      #aug = random.uniform(0.0,1.0)
      #if aug > 0.5 :
      x[j+1] = x[j][::-1,:]
      y[j+1] = y[j][::-1,:]
      #elif aug > 0.75 :
      x[j+2] = x[j][:,::-1]
      y[j+2] = y[j][:,::-1]
      #elif aug > 0.875 :
      x[j+3] = x[j][::-1,::-1]
      y[j+3] = y[j][::-1,::-1]
    return x, y
