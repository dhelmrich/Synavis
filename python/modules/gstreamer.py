import cv2
import numpy as np
import base64
import subprocess
import sys

# console arguments:
# -i <ip address>
# -p <port>

# default values
connection_ip = "127.0.0.1"
connection_port = 5000

# parse console arguments
for i in range(1,len(sys.argv)) :
  if sys.argv[i] == "-i" :
    connection_ip = sys.argv[i+1]
  if sys.argv[i] == "-p" :
    connection_port = int(sys.argv[i+1])


# here we assume the relay is already setup
# we just need to connect to it
# we use an opencv pipeline to decode the video

file = "udpsrc address=" + connection_ip + " port=" + str(connection_port) + \
    " caps=\"application/x-rtp\" ! queue ! rtph264depay " + \
    "! video/x-h264,stream-format=byte-stream ! queue ! avdec_h264 ! queue ! appsink"
pipe = cv2.VideoCapture(file,cv2.CAP_GSTREAMER)

if not pipe.isOpened() :
  # fetch opencv error
  print("Video capture could not open")
  exit(-1)


while True:
  ret, frame = pipe.read()
  if ret:
    # brief frame info
    print("frame shape: " + str(frame.shape))
  else:
    print("no frame")
    break
