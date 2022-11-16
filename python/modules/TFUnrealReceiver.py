import numpy as np
import tensorflow as tf

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
