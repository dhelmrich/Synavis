import sys
sys.path.append("build/receiver/Release/")
import numpy as np
import asyncio
import PyUnrealReceiver as UR
import threading
import time
import cv2



Receiver = UR.PyUnrealReceiver()
print(Receiver)
Receiver.UseConfig("./config.json")
Receiver.RegisterWithSignalling()

def StartReception() :
  global Receiver
  print("Delaying the start to give main thread a chance to continue")
  time.sleep(2)
  Receiver.RunForever()

def Test() :
  while True:
    time.sleep(2)
    

print("Running Thread")
clock = threading.Thread(target=Test, daemon = False)
bt = threading.Thread(target=StartReception, daemon = False)
clock.start()
bt.start()

print("Decoupled successfully")

time.sleep(7)
print("Slept through decoupling")
cache = np.array(Receiver.EmptyCache())

print(cache)

clock.join()
exit(-1)
