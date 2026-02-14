import numpy as np
import threading
import time
import sys
import os
import json
from collections import defaultdict, deque

# set working directory to script directory
olddir = os.getcwd()
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Synavis: Find build
# Prefer platform-appropriate build output directory
if os.name == 'nt':
  sys.path.append(r"C:/work/Synavis/build_win/synavis/Release/")
else:
  # Resolve the unix build path relative to this script
  script_dir = os.path.dirname(os.path.abspath(__file__))
  unix_build = os.path.abspath(os.path.join(script_dir, "../../build_unix"))
  sys.path.append(unix_build)

import PySynavis as syn

syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogVerbose)
#syn.VerboseMode(True)
syn.RegisterAvLogCallback(True)
pylog = syn.Logger()
pylog.setidentity("Synavis Unit Test")
# ensure previous logfile is rotated and a fresh logfile started
pylog.rotateLogFile("extraction.log")

pylog.log("Starting extraction module")

HEIGHT = 256
WIDTH = 256

import matplotlib
import matplotlib.pyplot as plt
plt.ion()
_plt_fig = None
_plt_ax = None
_plt_img = None
from queue import Queue, Empty

# queue for handing frames from worker thread to main thread for plotting
frame_queue = Queue(maxsize=8)

message_buffer = []

# statistics logging interval and accumulator
LOG_INTERVAL = 5.0
STATS = {
  'total_frames': 0,
  'bytes_total': 0,
  'by_resolution': defaultdict(int),
  'by_timestamp': defaultdict(int),
  'frame_times': deque(maxlen=1000),
  'last_log': time.time(),
  'last_total': 0,
  'last_bytes': 0
}

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
  # Normalize message to a str and log using a single argument
  if isinstance(msg, (bytes, bytearray)):
    s = msg.decode('utf-8', errors='replace')
  else:
    s = str(msg)
  pylog.log(f"Received message: {s}")
  message_buffer.append(s)

# a callback function for the data connector
def data_callback(data) :
  pylog.log("Received raw data packet of length {}".format(len(data)))

def frame_callback(frame, info=None) :
  global _plt_fig, _plt_ax, _plt_img
  # log type of frame and size
  pylog.log(f"Received frame of type {type(frame)} with size {len(frame.Data) if hasattr(frame, 'Data') else 'N/A'} bytes, timestamp={getattr(info, 'timestamp', None)}")
  # log frame info if available
  if info is not None:
    pylog.log(f"Frame info: timestamp={getattr(info, 'timestamp', None)}, width={getattr(info, 'width', None)}, height={getattr(info, 'height', None)}")
  # Attempt to interpret and display the received frame (live update)
  if not isinstance(frame, (bytes, bytearray)):
    w = getattr(frame, 'Width', None) or getattr(frame, 'width', None) or WIDTH
    h = getattr(frame, 'Height', None) or getattr(frame, 'height', None) or HEIGHT
    ts = getattr(frame, 'Timestamp', None) or (getattr(info, 'timestamp', None) if info is not None else None)
    raw = bytes(frame.Data)

    # detect YUV420, RGB or grayscale
    y_size = int(w) * int(h)
    uv_size = (int(w) // 2) * (int(h) // 2)
    expected = y_size + 2 * uv_size

    rgb = None
    if expected and len(raw) == expected:
      arr = np.frombuffer(raw, dtype=np.uint8)
      Y = arr[0:y_size].reshape((int(h), int(w)))
      U = arr[y_size:y_size + uv_size].reshape((int(h)//2, int(w)//2))
      V = arr[y_size + uv_size:].reshape((int(h)//2, int(w)//2))
      U_up = U.repeat(2, axis=0).repeat(2, axis=1)
      V_up = V.repeat(2, axis=0).repeat(2, axis=1)
      C = Y.astype(np.int32) - 16
      D = U_up.astype(np.int32) - 128
      E = V_up.astype(np.int32) - 128
      R = (298 * C + 409 * E + 128) >> 8
      G = (298 * C - 100 * D - 208 * E + 128) >> 8
      B = (298 * C + 516 * D + 128) >> 8
      rgb = np.stack([np.clip(R, 0, 255), np.clip(G, 0, 255), np.clip(B, 0, 255)], axis=-1).astype(np.uint8)
    else:
      if len(raw) == int(w) * int(h) * 3:
        rgb = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w), 3)).copy()
      elif len(raw) == int(w) * int(h):
        g = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w)))
        rgb = np.stack([g, g, g], axis=-1)

    # enqueue frame for plotting in main thread (matplotlib must run in main thread)
    if rgb is not None:
      try:
        frame_queue.put_nowait((rgb, ts))
      except Exception:
        # drop frame if queue is full
        pass

m = syn.MediaReceiver()
f = syn.FrameDecode(syn.Codec.VP9)

m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "172.21.96.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
# Register callbacks
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
# Create the acceptor and wrap it to log return values (so we can detect when it returns False)
_acceptor = f.CreateAcceptor(frame_callback)
def _acceptor_wrapper(data, info=None):
  size = len(data) if hasattr(data, '__len__') else -1
  pylog.log(f"Acceptor invoked: incoming size={size} ts={getattr(info, 'timestamp', None)}")
  ret = _acceptor(data, info)
  pylog.log(f"Acceptor returned: {ret} for size={size} ts={getattr(info, 'timestamp', None)}")
  return ret

# Register the single frame reception callback (acceptor wrapper)
m.SetFrameReceptionCallback(_acceptor_wrapper)


m.SetOnTrackOpenCallback(lambda: pylog.log("Track opened"))
# exit Python when the incoming track closes
m.SetOnTrackCloseCallback(lambda: syn.ExitWithMessage("Track closed", 1))
# exit Python when the data channel closes
m.SetOnClosedCallback(lambda: syn.ExitWithMessage("Data channel closed", 2))
m.SetRetryOnErrorResponse(True)
m.LockUntilConnected(2000)

pylog.log("Connected to media sender.")

# attempt to choose a sensible default datachannel (one that contains 'handler')
names = m.GetDataChannelNames()
pylog.log(f"Available datachannels: {names}")
for nm in names:
  if "handler" in nm.lower():
    if m.SelectDataChannelByName(nm):
      pylog.log(f"Selected datachannel '{nm}' as default")
      break

cnt = m.NumRemoteMedia()
pylog.log(f"NumRemoteMedia={cnt}")
for i in range(cnt):
  desc = m.RemoteMediaDescription(i)
  pylog.log(f"RemoteMediaDescription[{i}]={desc}")
  f.ParseDescription(desc)
pylog.log("Called FrameDecode.ParseDescription for all remote media")

# Helper: poll message buffer for the actor list response
def poll_for_actor_list(timeout=2.0):
  start = time.time()
  while time.time() - start < timeout:
    # iterate over a copy to allow removal
    for idx, raw in enumerate(list(message_buffer)):
      obj = json.loads(raw)
      if obj.get("type") == "query" and obj.get("name") == "all" and isinstance(obj.get("data"), list):
        # remove the matched entry from the real buffer
        for j in range(len(message_buffer)):
          if message_buffer[j] == raw:
            message_buffer.pop(j)
            break
        return obj.get("data")
    time.sleep(0.1)
  return None


def resolve_actor(prefix, timeout=2.0):
  # ask the scene for all actor names, then find one that contains the prefix
  m.SendJSON({"type": "query"})
  names = poll_for_actor_list(timeout)
  if not names:
    return None
  for n in names:
    if prefix.lower() in n.lower():
      return n
  return None


# firing a few test messages (small suite to exercise scene commands/queries)
# first resolve SceneCam actual name (UE may decorate names)
resolved_camera = resolve_actor("SceneCam", timeout=2.0)
if resolved_camera:
  pylog.log("Resolved SceneCam -> {}".format(resolved_camera))
else:
  resolved_camera = "SceneCam"

tests = [
  {"type": "query"},
  #{"type": "query", "object": resolved_camera, "property": "Position"},
  #{"type": "command", "name": "navigate", "x": 100.0, "y": 200.0, "z": 300.0},
  #{"type": "command", "name": "cam", "camera": "scene"},
  {"type": "console", "command": "t.MaxFPS 10"},
  {"type": "console", "command": "log LogTemp Verbose"},
  #{"type": "query", "spawn": "any"},
  #{"type": "track", "object": resolved_camera, "property": "Position"},
  #{"type": "untrack", "object": resolved_camera, "property": "Position"},
  {"type": "command", "name": "start"}
]

for t in tests:
  pylog.log("Sending test message: {}".format(t))
  m.SendJSON(t)
  time.sleep(0.3)

# keep the script running and handle plotting from the main thread
while True:
  try:
    # process one frame for plotting if available
    try:
      rgb, ts = frame_queue.get(timeout=0.1)
      if _plt_fig is None:
        _plt_fig, _plt_ax = plt.subplots()
        _plt_img = _plt_ax.imshow(rgb)
        _plt_ax.set_title(f"ts={ts}")
        _plt_fig.canvas.draw()
        plt.show(block=False)
      else:
        _plt_img.set_data(rgb)
        _plt_ax.set_title(f"ts={ts}")
        _plt_fig.canvas.draw_idle()
      plt.pause(0.001)
    except Empty:
      # no frame ready; yield to other work
      pass
    time.sleep(0.05)
  except KeyboardInterrupt:
    break
