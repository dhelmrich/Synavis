import sys
import os
sys.path.append("./build/receiver/Release/")
#print(os.environ["FFMPEG_PATH"])
#sys.path.append(os.environ["FFMPEG_PATH"])
import numpy as np
import asyncio
import PyUnrealReceiver as UR
import subprocess
import threading
import time
import ffmpeg
import cv2
import io
os.environ['OPENCV_FFMPEG_CAPTURE_OPTIONS'] = 'protocol_whitelist;file,rtp,udp'

width = 1024
height = 768

Receiver = UR.PyUnrealReceiver()
print(Receiver)
Receiver.UseConfig("./config.json")
Receiver.RegisterWithSignalling()
decoder = h264decoder.H264Decoder()

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

time.sleep(4)
print("Slept through decoupling")
with open("pixelstreaming.sdp","w") as f :
  f.write(Receiver.SessionDescriptionProtocol())
print("I wrote the sdp file, you can start ffmpeg now!")



# had .video after input
process = (
  ffmpeg
  .input('pixelstreaming.sdp', rtsp_flags = 'listen')
  .output('pipe:',format='rawvideo',pix_fmt='bgr24')
  .run_async(pipe_stdin=True,pipe_stdout=True)
)

cache = Receiver.EmptyCache()
print("Received ", len(cache), " frames.")

firstframe = np.array(cache[0],dtype=np.uint8)

process.stdin.write(firstframe)
process.stdin.close()

result = process.stdout.read(width*height*3)
if not result:
  print("Did not read any image")
  exit(-1)
else :
  frame_convert = np.frombuffer(result,np.uint8).reshape([height,width,3])
  cv2.imshow("test",frame_convert)


#img = decoder.decode(firstframe)
#print(firstframe)
#print(img)
#if img != None :
#  cv2.imshow("frame",img)

#video = cv2.VideoCapture(
#    'udpsrc address=127.0.0.1 port=5000 caps="application/x-rtp" ! queue ! rtph264depay ! video/x-h264,stream-format=byte-stream ! queue ! decodebin ! avdec_h264 ! videoconvert ! appsink'
#    , cv2.CAP_GSTREAMER)
#if not video.isOpened():
#  print('VideoCapture not opened')
#  exit(0)
#while True:
#  ret,frame = video.read()
#  if not ret:
#    print('empty frame')
#    break
#  cv2.imshow('receive', frame)
#  if cv2.waitKey(1)&0xFF == ord('q'):
#    break


clock.join()
process.wait()
cv2.destroyAllWindows()
exit(-1)
